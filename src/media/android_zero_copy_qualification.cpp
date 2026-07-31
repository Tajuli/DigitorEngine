#include "digitor/android_zero_copy_qualification.hpp"

namespace digitor {

AndroidZeroCopyQualificationDecision validate_android_zero_copy_evidence(
    const AndroidZeroCopyConfig& runtime,
    const AndroidZeroCopyQualificationEvidence& evidence,
    const AndroidZeroCopyQualificationPolicy& policy,
    std::uint64_t now_unix) noexcept {
  AndroidZeroCopyQualificationDecision d{};
  auto reject = [&](const char* message) noexcept {
    d.production_ready = false;
    d.diagnostic = message;
    return d;
  };

  if (!runtime.strict_gpu_first || !runtime.require_external_memory ||
      !runtime.require_rgba16f_output)
    return reject("runtime is not strict GPU-first");
  if (evidence.qualification_id.empty())
    return reject("qualification identity is missing");
  if (evidence.device_fingerprint != runtime.device_fingerprint)
    return reject("Android device fingerprint mismatch");
  if (evidence.gpu_name != runtime.gpu_name)
    return reject("Android GPU identity mismatch");
  if (evidence.driver_version != runtime.driver_version)
    return reject("Android GPU driver mismatch");
  if (evidence.engine_commit != runtime.engine_commit)
    return reject("engine commit mismatch");
  if (!evidence.generated_at_unix || !evidence.expires_at_unix ||
      now_unix < evidence.generated_at_unix || now_unix >= evidence.expires_at_unix)
    return reject("qualification evidence is expired or invalid");
  if (!evidence.mediacodec_surface_decode ||
      !evidence.ahardwarebuffer_import ||
      !evidence.external_fence_sync ||
      !evidence.nv12_pass ||
      !evidence.preview_export_identity ||
      !evidence.no_resource_leak ||
      !evidence.no_cpu_copy ||
      !evidence.no_cpu_fallback)
    return reject("mandatory Android zero-copy gate failed");
  if (policy.require_p010 && !evidence.p010_pass)
    return reject("P010 qualification failed");
  if (policy.require_hardware_encode && !evidence.hardware_encode)
    return reject("hardware encoder qualification failed");
  if (policy.require_stress && !evidence.sustained_stress)
    return reject("sustained stress qualification failed");
  if (evidence.measured_fps < policy.minimum_fps)
    return reject("measured Android zero-copy throughput is too low");
  if (evidence.p95_latency_ms > policy.maximum_p95_latency_ms)
    return reject("p95 Android zero-copy latency is too high");
  if (evidence.mean_absolute_error > policy.maximum_mean_error ||
      evidence.max_absolute_error > policy.maximum_max_error)
    return reject("per-pixel Android color accuracy gate failed");
  if (evidence.resource_delta > policy.maximum_resource_delta)
    return reject("Android GPU resource leak budget exceeded");

  d.production_ready = true;
  d.diagnostic = "Android zero-copy evidence accepted";
  return d;
}

} // namespace digitor
