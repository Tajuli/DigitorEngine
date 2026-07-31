#pragma once

#include "digitor/android_zero_copy_pipeline.hpp"

#include <cstdint>
#include <string>

namespace digitor {

struct AndroidZeroCopyQualificationEvidence {
  std::string qualification_id;
  std::string device_fingerprint;
  std::string gpu_name;
  std::string driver_version;
  std::string engine_commit;
  std::uint64_t generated_at_unix{};
  std::uint64_t expires_at_unix{};
  bool mediacodec_surface_decode{};
  bool ahardwarebuffer_import{};
  bool external_fence_sync{};
  bool nv12_pass{};
  bool p010_pass{};
  bool preview_export_identity{};
  bool hardware_encode{};
  bool sustained_stress{};
  bool no_resource_leak{};
  bool no_cpu_copy{};
  bool no_cpu_fallback{};
  double measured_fps{};
  double p95_latency_ms{};
  double mean_absolute_error{};
  double max_absolute_error{};
  std::int64_t resource_delta{};
};

struct AndroidZeroCopyQualificationPolicy {
  double minimum_fps{30.0};
  double maximum_p95_latency_ms{33.4};
  double maximum_mean_error{0.0015};
  double maximum_max_error{0.01};
  std::int64_t maximum_resource_delta{0};
  bool require_p010{true};
  bool require_hardware_encode{true};
  bool require_stress{true};
};

struct AndroidZeroCopyQualificationDecision {
  bool production_ready{};
  std::string diagnostic;
};

[[nodiscard]] AndroidZeroCopyQualificationDecision
validate_android_zero_copy_evidence(
    const AndroidZeroCopyConfig& runtime,
    const AndroidZeroCopyQualificationEvidence& evidence,
    const AndroidZeroCopyQualificationPolicy& policy,
    std::uint64_t now_unix) noexcept;

} // namespace digitor
