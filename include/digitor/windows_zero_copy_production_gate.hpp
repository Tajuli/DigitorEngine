#pragma once

#include <cstdint>
#include <string>

namespace digitor {

struct WindowsZeroCopyProductionEvidence {
  bool production_ready{};
  bool strict_gpu_first{};
  bool nv12_passed{};
  bool p010_passed{};
  bool preview_export_identity{};
  bool no_cpu_transfer{};
  bool pixel_accuracy{};
  bool stress_passed{};
  bool leak_free{};
  bool realtime_4k{};
  std::string adapter_luid;
  std::string driver_version;
  std::string engine_commit;
  std::string media_fingerprint;
  std::int64_t qualified_at_unix{};
};

struct WindowsZeroCopyProductionRequest {
  bool feature_flag_enabled{};
  bool strict_gpu_first{};
  std::string adapter_luid;
  std::string driver_version;
  std::string engine_commit;
  std::int64_t now_unix{};
  std::int64_t maximum_evidence_age_seconds{30LL*24*60*60};
};

struct WindowsZeroCopyProductionDecision {
  bool enabled{};
  std::string diagnostic;
};

[[nodiscard]] WindowsZeroCopyProductionDecision decide_windows_zero_copy_production(
    const WindowsZeroCopyProductionEvidence&,
    const WindowsZeroCopyProductionRequest&) noexcept;

} // namespace digitor
