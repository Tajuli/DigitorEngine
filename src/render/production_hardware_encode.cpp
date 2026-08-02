#include "digitor/production_hardware_encode.hpp"

#include <algorithm>
#include <utility>

namespace digitor {

bool ProductionHardwareEncodeSession::hardware_backend(EncoderBackend backend) noexcept {
  return backend != EncoderBackend::software;
}

ProductionHardwareEncodeSession::ProductionHardwareEncodeSession(
    HardwareEncodeConfig config, HardwareEncoderCallbacks callbacks)
    : config_(std::move(config)), callbacks_(std::move(callbacks)) {
  telemetry_.backend = config_.backend;
}

ProductionHardwareEncodeSession::~ProductionHardwareEncodeSession() {
  std::scoped_lock lock(mutex_);
  if (telemetry_.state == HardwareEncodeState::running ||
      telemetry_.state == HardwareEncodeState::draining) {
    if (callbacks_.cancel) callbacks_.cancel();
    telemetry_.state = HardwareEncodeState::cancelled;
  }
}

DigitorResult ProductionHardwareEncodeSession::fail_locked(
    std::string diagnostic, std::string* output) noexcept {
  telemetry_.state = HardwareEncodeState::failed;
  telemetry_.diagnostic = std::move(diagnostic);
  if (output) *output = telemetry_.diagnostic;
  return DIGITOR_RESULT_BACKEND_FAILURE;
}

DigitorResult ProductionHardwareEncodeSession::start(std::string* diagnostic) noexcept {
  std::scoped_lock lock(mutex_);
  if (telemetry_.state != HardwareEncodeState::idle)
    return fail_locked("hardware encode session already started", diagnostic);
  if (config_.output_path.empty())
    return fail_locked("hardware encode output path is empty", diagnostic);
  if (config_.profile.width <= 0 || config_.profile.height <= 0 ||
      config_.profile.fps_num <= 0 || config_.profile.fps_den <= 0)
    return fail_locked("invalid hardware encode profile", diagnostic);
  if (config_.require_hardware && !hardware_backend(config_.backend))
    return fail_locked("production session requires a hardware encoder", diagnostic);
  if (!callbacks_.open || !callbacks_.submit_gpu_frame || !callbacks_.drain ||
      !callbacks_.finalize_atomic)
    return fail_locked("hardware encoder callbacks are incomplete", diagnostic);

  std::string local;
  const auto result = callbacks_.open(config_, local);
  if (result != DIGITOR_RESULT_OK)
    return fail_locked(local.empty() ? "hardware encoder open failed" : local, diagnostic);
  opened_ = true;
  telemetry_.state = HardwareEncodeState::running;
  telemetry_.diagnostic.clear();
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

DigitorResult ProductionHardwareEncodeSession::submit(
    HardwareEncodeFrame frame, std::string* diagnostic) noexcept {
  std::scoped_lock lock(mutex_);
  ++telemetry_.submitted_frames;
  if (telemetry_.state != HardwareEncodeState::running) {
    ++telemetry_.rejected_frames;
    return fail_locked("hardware encoder is not running", diagnostic);
  }
  if (!frame.frame || !frame.frame->ready()) {
    ++telemetry_.rejected_frames;
    return fail_locked("GPU frame is missing or not ready", diagnostic);
  }
  if (!frame.frame->context_live()) {
    ++telemetry_.rejected_frames;
    return fail_locked("GPU frame context is retired", diagnostic);
  }
  if (config_.require_hardware && frame.frame->backend() == DIGITOR_RENDERER_CPU) {
    ++telemetry_.rejected_frames;
    ++telemetry_.cpu_readbacks;
    return fail_locked("CPU frame rejected by hardware encode session", diagnostic);
  }
  if (config_.require_zero_copy && frame.frame->direct_validation_readback_supported()) {
    // Readback capability may exist for qualification, but the encode path never calls it.
    // Keep the counter at zero to make accidental CPU conversion externally observable.
  }
  if (frame.pts_us < 0 || frame.duration_us <= 0) {
    ++telemetry_.rejected_frames;
    return fail_locked("invalid hardware encode timestamp", diagnostic);
  }
  if (config_.require_monotonic_timestamps && telemetry_.last_pts_us >= 0 &&
      frame.pts_us <= telemetry_.last_pts_us) {
    ++telemetry_.rejected_frames;
    return fail_locked("non-monotonic hardware encode timestamp", diagnostic);
  }
  const auto& metadata = frame.frame->metadata();
  if (metadata.width != static_cast<std::uint32_t>(config_.profile.width) ||
      metadata.height != static_cast<std::uint32_t>(config_.profile.height)) {
    ++telemetry_.rejected_frames;
    return fail_locked("GPU frame dimensions do not match encode profile", diagnostic);
  }

  std::string local;
  const auto result = callbacks_.submit_gpu_frame(frame, local);
  if (result != DIGITOR_RESULT_OK) {
    ++telemetry_.rejected_frames;
    return fail_locked(local.empty() ? "hardware encoder rejected GPU frame" : local,
                       diagnostic);
  }
  ++telemetry_.accepted_frames;
  telemetry_.last_pts_us = frame.pts_us;
  telemetry_.completed_us = std::max(telemetry_.completed_us,
                                     frame.pts_us + frame.duration_us);
  telemetry_.progress = config_.duration_us > 0
                            ? std::min(1.0, static_cast<double>(telemetry_.completed_us) /
                                                static_cast<double>(config_.duration_us))
                            : 0.0;
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

DigitorResult ProductionHardwareEncodeSession::finish(std::string* diagnostic) noexcept {
  std::scoped_lock lock(mutex_);
  if (telemetry_.state != HardwareEncodeState::running || !opened_)
    return fail_locked("hardware encoder cannot finish from current state", diagnostic);
  telemetry_.state = HardwareEncodeState::draining;

  std::string local;
  auto result = callbacks_.drain(local);
  if (result != DIGITOR_RESULT_OK)
    return fail_locked(local.empty() ? "hardware encoder drain failed" : local, diagnostic);
  result = callbacks_.finalize_atomic(local);
  if (result != DIGITOR_RESULT_OK)
    return fail_locked(local.empty() ? "atomic hardware export finalize failed" : local,
                       diagnostic);

  telemetry_.state = HardwareEncodeState::completed;
  telemetry_.progress = 1.0;
  telemetry_.diagnostic.clear();
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

void ProductionHardwareEncodeSession::cancel() noexcept {
  std::scoped_lock lock(mutex_);
  if (telemetry_.state == HardwareEncodeState::completed ||
      telemetry_.state == HardwareEncodeState::failed ||
      telemetry_.state == HardwareEncodeState::cancelled)
    return;
  if (callbacks_.cancel) callbacks_.cancel();
  telemetry_.state = HardwareEncodeState::cancelled;
  telemetry_.diagnostic = "hardware encode cancelled";
}

HardwareEncodeTelemetry ProductionHardwareEncodeSession::telemetry() const {
  std::scoped_lock lock(mutex_);
  return telemetry_;
}

}  // namespace digitor
