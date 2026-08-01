#include "digitor/unified_zero_copy_runtime.hpp"
#include <mutex>
#include <utility>

namespace digitor {

DigitorResult validate_zero_copy_evidence(const UnifiedZeroCopyConfig& c,
    const ZeroCopyQualificationEvidence& e, std::int64_t now,
    std::string& d) noexcept {
  if (e.platform != c.platform) { d="qualification platform mismatch"; return DIGITOR_RESULT_INVALID_ARGUMENT; }
  if (e.device_identity.empty() || e.device_identity != c.device_identity) { d="qualification device identity mismatch"; return DIGITOR_RESULT_BACKEND_UNAVAILABLE; }
  if (e.driver_or_os_build.empty() || e.driver_or_os_build != c.driver_or_os_build) { d="qualification driver or OS build mismatch"; return DIGITOR_RESULT_BACKEND_UNAVAILABLE; }
  if (e.engine_commit.empty() || e.engine_commit != c.engine_commit) { d="qualification engine commit mismatch"; return DIGITOR_RESULT_BACKEND_UNAVAILABLE; }
  if (e.qualification_id.empty() || e.valid_until_unix_seconds <= now) { d="qualification evidence missing or expired"; return DIGITOR_RESULT_BACKEND_UNAVAILABLE; }
  if ((c.require_strict_gpu_first && !e.strict_gpu_first) || !e.decode_zero_copy ||
      !e.render_zero_copy || (c.require_preview_export_identity && !e.preview_export_identity) ||
      !e.per_pixel_accuracy || !e.hardware_encode || !e.sustained_4k || !e.stress_and_leak) {
    d="mandatory zero-copy qualification gate failed"; return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (e.minimum_fps <= 0.0 || e.measured_fps < e.minimum_fps) { d="sustained throughput below qualification minimum"; return DIGITOR_RESULT_BACKEND_UNAVAILABLE; }
  if (e.allowed_mean_error < 0.0 || e.max_mean_error > e.allowed_mean_error) { d="per-pixel mean error exceeds qualification limit"; return DIGITOR_RESULT_BACKEND_UNAVAILABLE; }
  if (e.resource_delta > e.allowed_resource_delta) { d="resource leak delta exceeds qualification budget"; return DIGITOR_RESULT_BACKEND_UNAVAILABLE; }
  d="zero-copy evidence accepted"; return DIGITOR_RESULT_OK;
}

struct UnifiedZeroCopyRuntime::Impl {
  UnifiedZeroCopyConfig config;
  UnifiedZeroCopyBinding binding;
  mutable std::mutex mutex;
  UnifiedZeroCopyTelemetry telemetry;
  ZeroCopyQualificationEvidence evidence;
  std::uint32_t consecutive_failures{};
};

UnifiedZeroCopyRuntime::UnifiedZeroCopyRuntime(UnifiedZeroCopyConfig c, UnifiedZeroCopyBinding b)
    : impl_(std::make_unique<Impl>()) { impl_->config=std::move(c); impl_->binding=std::move(b); }
UnifiedZeroCopyRuntime::~UnifiedZeroCopyRuntime() = default;

DigitorResult UnifiedZeroCopyRuntime::activate(const ZeroCopyQualificationEvidence& e, std::int64_t now) noexcept {
  try {
    auto& i=*impl_;
    if (!i.binding.preview || !i.binding.export_frame || !i.binding.preview_and_export) return DIGITOR_RESULT_INVALID_ARGUMENT;
    std::string d; const auto r=validate_zero_copy_evidence(i.config,e,now,d);
    std::scoped_lock lock(i.mutex); i.telemetry.diagnostic=d;
    if (r!=DIGITOR_RESULT_OK) { i.telemetry.state=ZeroCopyRuntimeState::disabled; return r; }
    i.evidence=e; i.consecutive_failures=0; i.telemetry={};
    i.telemetry.state=ZeroCopyRuntimeState::active;
    i.telemetry.diagnostic="unified zero-copy production runtime active";
    return DIGITOR_RESULT_OK;
  } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

template <typename ImplT>
static DigitorResult run_action(ImplT& i, const ZeroCopyFrameAction& action,
    std::int64_t timestamp_us, bool preview, bool export_frame, bool shared) noexcept {
  std::scoped_lock lock(i.mutex);
  if (i.telemetry.state != ZeroCopyRuntimeState::active) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  const auto r=action(timestamp_us);
  if (r==DIGITOR_RESULT_OK) {
    i.consecutive_failures=0;
    if (preview) ++i.telemetry.preview_frames;
    if (export_frame) ++i.telemetry.export_frames;
    if (shared) ++i.telemetry.shared_frame_reuses;
    i.telemetry.diagnostic="zero-copy frame completed";
    return DIGITOR_RESULT_OK;
  }
  ++i.telemetry.failures; ++i.consecutive_failures;
  i.telemetry.diagnostic="platform zero-copy frame failed";
  const auto limit=i.config.quarantine_after_failures==0 ? 1u : i.config.quarantine_after_failures;
  if (i.consecutive_failures>=limit) { i.telemetry.state=ZeroCopyRuntimeState::quarantined; i.telemetry.diagnostic="zero-copy runtime quarantined"; }
  return r;
}

DigitorResult UnifiedZeroCopyRuntime::preview(std::int64_t t) noexcept { return run_action(*impl_,impl_->binding.preview,t,true,false,false); }
DigitorResult UnifiedZeroCopyRuntime::export_frame(std::int64_t t) noexcept { return run_action(*impl_,impl_->binding.export_frame,t,false,true,false); }
DigitorResult UnifiedZeroCopyRuntime::preview_and_export(std::int64_t t) noexcept { return run_action(*impl_,impl_->binding.preview_and_export,t,true,true,true); }

DigitorResult UnifiedZeroCopyRuntime::reset_quarantine(const ZeroCopyQualificationEvidence& e, std::int64_t now) noexcept {
  try {
    auto& i=*impl_; std::string d; auto r=validate_zero_copy_evidence(i.config,e,now,d); if (r!=DIGITOR_RESULT_OK) return r;
    if (i.binding.reset_platform_quarantine) { r=i.binding.reset_platform_quarantine(); if (r!=DIGITOR_RESULT_OK) return r; }
    std::scoped_lock lock(i.mutex); i.evidence=e; i.consecutive_failures=0;
    i.telemetry.state=ZeroCopyRuntimeState::active;
    i.telemetry.diagnostic="zero-copy quarantine reset with current evidence";
    return DIGITOR_RESULT_OK;
  } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

UnifiedZeroCopyTelemetry UnifiedZeroCopyRuntime::telemetry() const { std::scoped_lock lock(impl_->mutex); return impl_->telemetry; }
bool UnifiedZeroCopyRuntime::production_ready() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->telemetry.state==ZeroCopyRuntimeState::active && impl_->telemetry.cpu_copies==0 && impl_->telemetry.cpu_fallback_frames==0;
}

} // namespace digitor
