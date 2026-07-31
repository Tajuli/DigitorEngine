#include "digitor/windows_zero_copy_production_gate.hpp"

#include <cassert>

using namespace digitor;
int main(){
  WindowsZeroCopyProductionEvidence e{};
  e.production_ready=e.strict_gpu_first=e.nv12_passed=e.p010_passed=true;
  e.preview_export_identity=e.no_cpu_transfer=e.pixel_accuracy=true;
  e.stress_passed=e.leak_free=e.realtime_4k=true;
  e.adapter_luid="gpu-1";e.driver_version="1.2.3";e.engine_commit="abc";
  e.media_fingerprint="fixtures-v1";e.qualified_at_unix=1000;
  WindowsZeroCopyProductionRequest r{};r.feature_flag_enabled=true;r.strict_gpu_first=true;
  r.adapter_luid="gpu-1";r.driver_version="1.2.3";r.engine_commit="abc";r.now_unix=1100;
  assert(decide_windows_zero_copy_production(e,r).enabled);
  r.driver_version="changed";assert(!decide_windows_zero_copy_production(e,r).enabled);
  r.driver_version="1.2.3";r.engine_commit="changed";assert(!decide_windows_zero_copy_production(e,r).enabled);
  r.engine_commit="abc";r.now_unix=e.qualified_at_unix+r.maximum_evidence_age_seconds+1;
  assert(!decide_windows_zero_copy_production(e,r).enabled);
  r.now_unix=1100;r.feature_flag_enabled=false;assert(!decide_windows_zero_copy_production(e,r).enabled);
  return 0;
}
