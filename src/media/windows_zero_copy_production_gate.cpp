#include "digitor/windows_zero_copy_production_gate.hpp"

namespace digitor {
WindowsZeroCopyProductionDecision decide_windows_zero_copy_production(
    const WindowsZeroCopyProductionEvidence& e,
    const WindowsZeroCopyProductionRequest& r) noexcept {
  auto deny=[](const char* why){return WindowsZeroCopyProductionDecision{false,why};};
  if(!r.feature_flag_enabled)return deny("Windows zero-copy feature flag is disabled");
  if(!r.strict_gpu_first||!e.strict_gpu_first)return deny("strict GPU-first policy is required");
  if(!e.production_ready||!e.nv12_passed||!e.p010_passed||
     !e.preview_export_identity||!e.no_cpu_transfer||!e.pixel_accuracy||
     !e.stress_passed||!e.leak_free||!e.realtime_4k)
    return deny("qualification evidence is incomplete");
  if(e.adapter_luid.empty()||e.adapter_luid!=r.adapter_luid)
    return deny("qualification adapter does not match the runtime adapter");
  if(e.driver_version.empty()||e.driver_version!=r.driver_version)
    return deny("GPU driver changed after qualification");
  if(e.engine_commit.empty()||e.engine_commit!=r.engine_commit)
    return deny("engine build changed after qualification");
  if(e.media_fingerprint.empty())return deny("qualification media fingerprint is missing");
  if(e.qualified_at_unix<=0||r.now_unix<e.qualified_at_unix||
     r.now_unix-e.qualified_at_unix>r.maximum_evidence_age_seconds)
    return deny("qualification evidence is expired or invalid");
  return {true,"Windows zero-copy production path enabled"};
}
} // namespace digitor
