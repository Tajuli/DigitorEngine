#include "digitor/production_gpu_export_orchestrator.hpp"

#include <exception>
#include <utility>

namespace digitor {

ProductionGpuExportOrchestrator::ProductionGpuExportOrchestrator(
    std::shared_ptr<const ExportRenderSnapshot> snapshot,
    TimelineRenderRuntime& timeline,
    HardwareEncoderCallbacks encoder_callbacks,
    ProductionGpuExportCallbacks callbacks)
    : snapshot_(std::move(snapshot)), timeline_(timeline),
      encoder_callbacks_(std::move(encoder_callbacks)), callbacks_(std::move(callbacks)) {
  if (snapshot_) telemetry_.snapshot_identity = snapshot_->identity();
}

bool ProductionGpuExportOrchestrator::cancelled_locked() const {
  return cancel_requested_.load(std::memory_order_acquire) ||
         (callbacks_.cancelled && callbacks_.cancelled());
}

void ProductionGpuExportOrchestrator::cleanup_partial_locked() noexcept {
  if (cleanup_called_) return;
  cleanup_called_ = true;
  if (callbacks_.remove_partial_output) callbacks_.remove_partial_output();
}

DigitorResult ProductionGpuExportOrchestrator::fail_locked(
    DigitorResult result, std::string diagnostic, std::string* output) noexcept {
  telemetry_.state = result == DIGITOR_RESULT_RESOURCE_IN_USE
                         ? ProductionGpuExportState::cancelled
                         : ProductionGpuExportState::failed;
  telemetry_.diagnostic = std::move(diagnostic);
  cleanup_partial_locked();
  if (output) *output = telemetry_.diagnostic;
  return result;
}

DigitorResult ProductionGpuExportOrchestrator::execute(
    const std::vector<ExportFrameTiming>& schedule,
    std::string* diagnostic) noexcept {
  try {
    {
      std::scoped_lock lock(mutex_);
      if (telemetry_.state != ProductionGpuExportState::idle)
        return fail_locked(DIGITOR_RESULT_RESOURCE_IN_USE,
                           "GPU export orchestrator already used", diagnostic);
      if (!snapshot_)
        return fail_locked(DIGITOR_RESULT_INVALID_ARGUMENT,
                           "missing immutable export snapshot", diagnostic);
      const auto contract = validate_export_snapshot(*snapshot_);
      if (!contract) return fail_locked(contract.result, contract.diagnostic, diagnostic);
      if (!export_policy_uses_gpu(snapshot_->policy()))
        return fail_locked(DIGITOR_RESULT_UNSUPPORTED,
                           "GPU orchestrator accepts hardware-required snapshots only", diagnostic);
      if (schedule.empty())
        return fail_locked(DIGITOR_RESULT_INVALID_ARGUMENT,
                           "export frame schedule is empty", diagnostic);
      std::int64_t previous_pts = -1;
      for (const auto& item : schedule) {
        if (item.pts_us < 0 || item.duration_us <= 0 || item.pts_us <= previous_pts)
          return fail_locked(DIGITOR_RESULT_INVALID_ARGUMENT,
                             "export schedule must be valid and strictly monotonic", diagnostic);
        previous_pts = item.pts_us;
      }
      telemetry_.requested_frames = schedule.size();
      telemetry_.state = ProductionGpuExportState::running;
    }

    const auto& frozen = snapshot_->data();
    HardwareEncodeConfig encode_config{};
    encode_config.profile = frozen.profile;
    encode_config.backend = frozen.encoder_backend;
    encode_config.output_path = frozen.output_path;
    encode_config.duration_us = frozen.duration_us;
    encode_config.require_hardware = true;
    encode_config.require_zero_copy = true;
    encode_config.require_monotonic_timestamps = true;
    encode_config.require_atomic_finalize = true;

    ProductionHardwareEncodeSession encoder(std::move(encode_config), encoder_callbacks_);
    std::string local;
    auto result = encoder.start(&local);
    if (result != DIGITOR_RESULT_OK) {
      std::scoped_lock lock(mutex_);
      return fail_locked(result, local.empty() ? "hardware encoder start failed" : local,
                         diagnostic);
    }

    for (const auto& timing : schedule) {
      if (cancel_requested_.load(std::memory_order_acquire) ||
          (callbacks_.cancelled && callbacks_.cancelled())) {
        encoder.cancel();
        std::scoped_lock lock(mutex_);
        return fail_locked(DIGITOR_RESULT_RESOURCE_IN_USE, "GPU export cancelled", diagnostic);
      }

      auto rendered = timeline_.render(
          TimelineExecutionMode::export_render, timing.pts_us, frozen.width, frozen.height,
          frozen.timeline_revision, frozen.render_revision, 1024, false);
      if (rendered.cancelled) {
        encoder.cancel();
        std::scoped_lock lock(mutex_);
        return fail_locked(DIGITOR_RESULT_RESOURCE_IN_USE, "timeline render cancelled", diagnostic);
      }
      if (!rendered.success || !rendered.gpu_resident || !rendered.video.gpu ||
          !rendered.video.rgba.empty()) {
        encoder.cancel();
        std::scoped_lock lock(mutex_);
        ++telemetry_.cpu_staging_frames;
        return fail_locked(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                           rendered.diagnostic.empty()
                               ? "timeline did not produce an exclusive GPU-resident frame"
                               : rendered.diagnostic,
                           diagnostic);
      }

      const auto frame_contract =
          validate_frame_against_snapshot(*snapshot_, *rendered.video.gpu);
      if (!frame_contract) {
        encoder.cancel();
        std::scoped_lock lock(mutex_);
        return fail_locked(frame_contract.result, frame_contract.diagnostic, diagnostic);
      }
      if (rendered.video.gpu->metadata().timestamp != timing.pts_us) {
        encoder.cancel();
        std::scoped_lock lock(mutex_);
        return fail_locked(DIGITOR_RESULT_INVALID_ARGUMENT,
                           "rendered frame timestamp differs from frozen schedule", diagnostic);
      }

      {
        std::scoped_lock lock(mutex_);
        ++telemetry_.rendered_frames;
      }

      if (callbacks_.submit_audio && !rendered.audio.interleaved_stereo.empty()) {
        local.clear();
        result = callbacks_.submit_audio(rendered.audio, timing.pts_us, local);
        if (result != DIGITOR_RESULT_OK) {
          encoder.cancel();
          std::scoped_lock lock(mutex_);
          return fail_locked(result, local.empty() ? "audio mux submission failed" : local,
                             diagnostic);
        }
        std::scoped_lock lock(mutex_);
        ++telemetry_.audio_blocks;
      }

      local.clear();
      result = encoder.submit({rendered.video.gpu, timing.pts_us,
                               timing.duration_us, timing.force_keyframe}, &local);
      if (result != DIGITOR_RESULT_OK) {
        encoder.cancel();
        std::scoped_lock lock(mutex_);
        return fail_locked(result,
                           local.empty() ? "hardware encoder rejected GPU frame" : local,
                           diagnostic);
      }
      {
        std::scoped_lock lock(mutex_);
        ++telemetry_.encoded_frames;
        telemetry_.completed_us = timing.pts_us + timing.duration_us;
      }
    }

    {
      std::scoped_lock lock(mutex_);
      telemetry_.state = ProductionGpuExportState::draining;
    }
    local.clear();
    result = encoder.finish(&local);
    if (result != DIGITOR_RESULT_OK) {
      std::scoped_lock lock(mutex_);
      return fail_locked(result, local.empty() ? "hardware encoder finalize failed" : local,
                         diagnostic);
    }

    const auto encode_telemetry = encoder.telemetry();
    std::scoped_lock lock(mutex_);
    telemetry_.cpu_readbacks = encode_telemetry.cpu_readbacks;
    if (telemetry_.cpu_readbacks != 0 || telemetry_.cpu_staging_frames != 0)
      return fail_locked(DIGITOR_RESULT_INTERNAL_ERROR,
                         "zero-copy invariant violated", diagnostic);
    telemetry_.state = ProductionGpuExportState::completed;
    telemetry_.diagnostic.clear();
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    std::scoped_lock lock(mutex_);
    return fail_locked(DIGITOR_RESULT_OUT_OF_MEMORY,
                       "GPU export orchestration ran out of memory", diagnostic);
  } catch (const std::exception& error) {
    std::scoped_lock lock(mutex_);
    return fail_locked(DIGITOR_RESULT_INTERNAL_ERROR, error.what(), diagnostic);
  } catch (...) {
    std::scoped_lock lock(mutex_);
    return fail_locked(DIGITOR_RESULT_INTERNAL_ERROR,
                       "unknown GPU export orchestration failure", diagnostic);
  }
}

void ProductionGpuExportOrchestrator::cancel() noexcept {
  cancel_requested_.store(true, std::memory_order_release);
}

ProductionGpuExportTelemetry ProductionGpuExportOrchestrator::telemetry() const {
  std::scoped_lock lock(mutex_);
  return telemetry_;
}

}  // namespace digitor
