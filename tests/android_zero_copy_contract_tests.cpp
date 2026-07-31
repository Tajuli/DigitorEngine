#include "digitor/android_zero_copy_pipeline.hpp"
#include "digitor/android_zero_copy_qualification.hpp"
#include <cassert>

int main(){
  using namespace digitor;
  AndroidZeroCopyConfig invalid{};
  AndroidZeroCopyBinding empty{};
  AndroidZeroCopyPipeline p(invalid,empty);
  assert(p.initialize()==DIGITOR_RESULT_INVALID_ARGUMENT);

  AndroidZeroCopyConfig runtime{};
  runtime.strict_gpu_first=true;
  runtime.require_external_memory=true;
  runtime.require_rgba16f_output=true;
  runtime.device_fingerprint="device";
  runtime.gpu_name="gpu";
  runtime.driver_version="driver";
  runtime.engine_commit="commit";

  AndroidZeroCopyQualificationEvidence e{};
  e.qualification_id="q";
  e.device_fingerprint="device";
  e.gpu_name="gpu";
  e.driver_version="driver";
  e.engine_commit="commit";
  e.generated_at_unix=100;
  e.expires_at_unix=1000;
  e.mediacodec_surface_decode=true;
  e.ahardwarebuffer_import=true;
  e.external_fence_sync=true;
  e.nv12_pass=true;
  e.p010_pass=true;
  e.preview_export_identity=true;
  e.hardware_encode=true;
  e.sustained_stress=true;
  e.no_resource_leak=true;
  e.no_cpu_copy=true;
  e.no_cpu_fallback=true;
  e.measured_fps=60.0;
  e.p95_latency_ms=12.0;
  e.mean_absolute_error=0.0005;
  e.max_absolute_error=0.005;
  e.resource_delta=0;

  AndroidZeroCopyQualificationPolicy policy{};
  auto d=validate_android_zero_copy_evidence(runtime,e,policy,500);
  assert(d.production_ready);
  e.driver_version="wrong";
  d=validate_android_zero_copy_evidence(runtime,e,policy,500);
  assert(!d.production_ready);
  return 0;
}
