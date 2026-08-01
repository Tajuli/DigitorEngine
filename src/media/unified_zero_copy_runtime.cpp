#include "digitor/unified_zero_copy_runtime.hpp"

#include <mutex>
#include <utility>

namespace digitor {

DigitorResult validate_zero_copy_evidence(
    const UnifiedZeroCopyConfig& config,
    const ZeroCopyQualificationEvidence& evidence,
    std::int64_t now,
    std::string& diagnostic) noexcept {
  if (evidence.platform != config.platform) {
    diagnostic = "qualification platform mismatch";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (evidence.device_identity.empty() || evidence.device_identity != config.device_identity) {
    diagnostic = "qualification device identity mismatch";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (evidence.driver_or_os_build.empty() ||
      evidence.driver_or_os_build != config.driver_or_os_build) {
    diagnostic = "qualification driver or OS build mismatch";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (evidence.engine_commit.empty() || evidence.engine_commit != config.engine_commit) {
    diagnostic = "qualification engine commit mismatch";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (evidence.qualification_id.empty() || evidence.valid_until_unix_seconds <= now) {
    diagnostic = "qualification evidence missing or expired";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if ((config.require_strict_gpu_first && !evidence.strict_gpu_first) ||
      !evidence.decode_zero_copy || !evidence.render_zero_copy ||
      (config.require_preview_export_identity && !evidence.preview_export_identity) ||
      !evidence.per_pixel_accuracy || !evidence.hardware_encode ||
      !evidence.sustained_4k || !evidence.stress_and_leak) {
    diagnostic = "mandatory zero-copy qualification gate failed";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (evidence.minimum_fps <= 0.0 || evidence.measured_fps < evidence.minimum_fps) {
    diagnostic = "sustained throughput below qualification minimum";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (evidence.allowed_mean_error < 0.0 ||
      evidence.max_mean_error > evidence.allowed_mean_error) {
    diagnostic = "per-pixel mean error exceeds qualification limit";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (evidence.resource_delta > evidence.allowed_resource_delta) {
    diagnostic = "resource leak delta exceeds qualification budget";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  diagnostic = "zero-copy evidence accepted";
  return DIGITOR_RESULT_OK;
}

struct UnifiedZeroCopyRuntime::Impl {
  UnifiedZeroCopyConfig config;
  UnifiedZeroCopyBinding binding;
  mutable std::mutex mutex;
  UnifiedZeroCopyTelemetry telemetry;
  ZeroCopyQualificationEvidence evidence;
  std::uint32_t consecutive_failures{};
};

UnifiedZeroCopyRuntime::UnifiedZeroCopyRuntime(
    UnifiedZeroCopyConfig config, UnifiedZeroCopyBinding binding)
    : impl_(std::make_unique<Impl>()) {
  impl_->config = std::move(config);
  impl_->binding = std::move(binding);
}

UnifiedZeroCopyRuntime::~UnifiedZeroCopyRuntime() = default;

DigitorResult UnifiedZeroCopyRuntime::activate(
    const ZeroCopyQualificationEvidence& evidence, std::int64_t now) noexcept {
  try {
    auto& i = *impl_;
    if (!i.binding.preview || !i.binding.export_frame || !i.binding.preview_and_export)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    std::string diagnostic;
    const auto r = validate_zero_copy_evidence(i.config, evidence, now, diagnostic);
    std::scoped_lock lock(i.mutex);
    i.telemetry.diagnostic = diagnostic;
    if (r != DIGITOR_RESULT_OK) {
      i.telemetry.state = ZeroCopyRuntimeState::disabled;
      return r;
    }
    i.evidence = evidence;
    i.consecutive_failures = 0;
    i.telemetry = {};
    i.telemetry.state = ZeroCopyRuntimeState::active;
    i.telemetry.diagnostic = "unified zero-copy production runtime active";
    return DIGITOR_RESULT_OK;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

static DigitorResult run_action(UnifiedZeroCopyRuntime::Impl& i,
                                const ZeroCopyFrameAction& action,
                                std::int64_t timestamp_us,
                                bool preview, bool export_frame,
                                bool shared) noexcept {
  std::scoped_lock lock(i.mutex);
  if (i.telemetry.state != ZeroCopyRuntimeState::active)
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  const auto r = action(timestamp_us);
  if (r == DIGITOR_RESULT_OK) {
    i.consecutive_failures = 0;
    if (preview) ++i.telemetry.preview_frames;
    if (export_frame) ++i.telemetry.export_frames;
    if (shared) ++i.telemetry.shared_frame_reuses;
    i.telemetry.diagnostic = "zero-copy frame completed";
    return DIGITOR_RESULT_OK;
  }
  ++i.telemetry.failures;
  ++i.consecutive_failures;
  i.telemetry.diagnostic = "platform zero-copy frame failed";
  const auto limit = i.config.quarantine_after_failures == 0
      ? 1u : i.config.quarantine_after_failures;
  if (i.consecutive_failures >= limit) {
    i.telemetry.state = ZeroCopyRuntimeState::quarantined;
    i.telemetry.diagnostic = "zero-copy runtime quarantined";
  }
  return r;
}

DigitorResult UnifiedZeroCopyRuntime::preview(std::int64_t t) noexcept {
  return run_action(*impl_, impl_->binding.preview, t, true, false, false);
}

DigitorResult UnifiedZeroCopyRuntime::export_frame(std::int64_t t) noexcept {
  return run_action(*impl_, impl_->binding.export_frame, t, false, true, false);
}

DigitorResult UnifiedZeroCopyRuntime::preview_and_export(std::int64_t t) noexcept {
  return run_action(*impl_, impl_->binding.preview_and_export, t, true, true, true);
}

DigitorResult UnifiedZeroCopyRuntime::reset_quarantine(
    const ZeroCopyQualificationEvidence& evidence, std::int64_t now) noexcept {
  try {
    auto& i = *impl_;
    std::string diagnostic;
    auto r = validate_zero_copy_evidence(i.config, evidence, now, diagnostic);
    if (r != DIGITOR_RESULT_OK) return r;
    if (i.binding.reset_platform_quarantine) {
      r = i.binding.reset_platform_quarantine();
      if (r != DIGITOR_RESULT_OK) return r;
    }
    std::scoped_lock lock(i.mutex);
    i.evidence = evidence;
    i.consecutive_failures = 0;
    i.telemetry.state = ZeroCopyRuntimeState::active;
    i.telemetry.diagnostic = "zero-copy quarantine reset with current evidence";
    return DIGITOR_RESULT_OK;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

UnifiedZeroCopyTelemetry UnifiedZeroCopyRuntime::telemetry() const {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry;
}

bool UnifiedZeroCopyRuntime::production_ready() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry.state == ZeroCopyRuntimeState::active &&
         impl_->telemetry.cpu_copies == 0 &&
         impl_->telemetry.cpu_fallback_frames == 0;
}

} // namespace digitor
