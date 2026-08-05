#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class EvidenceBackend : std::uint32_t {
  vulkan = 1,
  d3d12 = 2,
  metal = 3,
  gles = 4,
};

enum class EvidencePlatform : std::uint32_t {
  windows = 1,
  android = 2,
  macos = 3,
  ios = 4,
};

enum class EvidenceStatus : std::uint32_t {
  invalid = 0,
  blocked = 1,
  qualified = 2,
};

struct HardwareEvidenceRecord {
  EvidencePlatform platform{EvidencePlatform::windows};
  EvidenceBackend backend{EvidenceBackend::vulkan};
  std::string device_id;
  std::string gpu_name;
  std::string driver_version;
  std::string engine_version;
  std::string source_commit;
  std::string evidence_sha256;
  std::uint64_t rendered_frames{0};
  std::uint64_t dropped_frames{0};
  std::uint64_t validation_errors{0};
  std::uint64_t device_loss_events{0};
  std::uint64_t soak_seconds{0};
  double average_frame_ms{0.0};
  double p95_frame_ms{0.0};
  std::uint64_t preview_digest{0};
  std::uint64_t export_digest{0};
  bool real_device{false};
  bool signed_attestation{false};
  bool gpu_execution_observed{false};
  bool silent_cpu_fallback_observed{false};
};

struct HardwareEvidencePolicy {
  std::uint64_t minimum_frames{18000};
  std::uint64_t minimum_soak_seconds{600};
  std::uint64_t maximum_dropped_frames{0};
  std::uint64_t maximum_validation_errors{0};
  std::uint64_t maximum_device_loss_events{0};
  double maximum_average_frame_ms{33.334};
  double maximum_p95_frame_ms{50.0};
  bool require_signed_attestation{true};
  bool require_preview_export_parity{true};
};

struct HardwareEvidenceResult {
  EvidenceStatus status{EvidenceStatus::invalid};
  std::uint32_t qualified_platform_mask{0};
  std::uint32_t missing_platform_mask{0};
  std::uint32_t failed_record_index{0xFFFFFFFFu};
  std::uint64_t digest{0};
};

HardwareEvidenceResult validate_hardware_evidence(
    const std::vector<HardwareEvidenceRecord>& records,
    const HardwareEvidencePolicy& policy) noexcept;

}  // namespace digitor

extern "C" {

typedef struct DigitorHardwareEvidenceRecord {
  std::uint32_t platform;
  std::uint32_t backend;
  const char* device_id;
  const char* gpu_name;
  const char* driver_version;
  const char* engine_version;
  const char* source_commit;
  const char* evidence_sha256;
  std::uint64_t rendered_frames;
  std::uint64_t dropped_frames;
  std::uint64_t validation_errors;
  std::uint64_t device_loss_events;
  std::uint64_t soak_seconds;
  double average_frame_ms;
  double p95_frame_ms;
  std::uint64_t preview_digest;
  std::uint64_t export_digest;
  std::uint32_t real_device;
  std::uint32_t signed_attestation;
  std::uint32_t gpu_execution_observed;
  std::uint32_t silent_cpu_fallback_observed;
} DigitorHardwareEvidenceRecord;

typedef struct DigitorHardwareEvidencePolicy {
  std::uint64_t minimum_frames;
  std::uint64_t minimum_soak_seconds;
  std::uint64_t maximum_dropped_frames;
  std::uint64_t maximum_validation_errors;
  std::uint64_t maximum_device_loss_events;
  double maximum_average_frame_ms;
  double maximum_p95_frame_ms;
  std::uint32_t require_signed_attestation;
  std::uint32_t require_preview_export_parity;
} DigitorHardwareEvidencePolicy;

typedef struct DigitorHardwareEvidenceResult {
  std::uint32_t status;
  std::uint32_t qualified_platform_mask;
  std::uint32_t missing_platform_mask;
  std::uint32_t failed_record_index;
  std::uint64_t digest;
} DigitorHardwareEvidenceResult;

std::uint32_t digitor_validate_hardware_evidence(
    const DigitorHardwareEvidenceRecord* records,
    std::uint32_t record_count,
    const DigitorHardwareEvidencePolicy* policy,
    DigitorHardwareEvidenceResult* output);

}