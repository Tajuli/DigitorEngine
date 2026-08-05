#pragma once

#include <cstdint>

namespace digitor {

enum class GpuProofBackend : std::uint32_t {
  cpu = 0u,
  vulkan = 1u,
  d3d12 = 2u,
  metal = 3u,
  gles = 4u,
};

enum class GpuProofPlatform : std::uint32_t {
  unknown = 0u,
  windows = 1u,
  android = 2u,
  macos = 3u,
  ios = 4u,
};

enum class GpuProofStatus : std::uint32_t {
  invalid = 0u,
  qualified = 1u,
  backend_mismatch = 2u,
  not_submitted = 3u,
  not_completed = 4u,
  missing_resource = 5u,
  missing_timing = 6u,
  output_unverified = 7u,
  cpu_fallback_detected = 8u,
  backend_error = 9u,
  replay_detected = 10u,
};

struct GpuExecutionProof final {
  GpuProofPlatform platform{GpuProofPlatform::unknown};
  GpuProofBackend backend{GpuProofBackend::cpu};
  std::uint64_t command_buffer_handle{};
  std::uint64_t queue_handle{};
  std::uint64_t input_resource_handle{};
  std::uint64_t output_resource_handle{};
  std::uint64_t submission_serial{};
  std::uint64_t completion_value{};
  std::uint64_t gpu_timestamp_begin_ns{};
  std::uint64_t gpu_timestamp_end_ns{};
  std::uint64_t output_digest{};
  std::uint32_t submitted{};
  std::uint32_t completed{};
  std::uint32_t output_written{};
  std::uint32_t cpu_fallback_observed{};
  std::uint32_t backend_error_count{};
};

struct GpuProofResult final {
  GpuProofStatus status{GpuProofStatus::invalid};
  std::uint64_t proof_digest{};
  std::uint64_t gpu_duration_ns{};
};

class GpuProofSequence final {
 public:
  GpuProofResult validate(const GpuExecutionProof& proof) noexcept;
  void reset() noexcept;

 private:
  std::uint64_t last_submission_serial_{};
  std::uint64_t last_completion_value_{};
};

GpuProofResult validate_gpu_execution_proof(const GpuExecutionProof& proof) noexcept;
std::uint64_t gpu_execution_proof_digest(const GpuExecutionProof& proof) noexcept;

}  // namespace digitor

extern "C" {

typedef struct DigitorGpuExecutionProof {
  std::uint32_t platform;
  std::uint32_t backend;
  std::uint64_t command_buffer_handle;
  std::uint64_t queue_handle;
  std::uint64_t input_resource_handle;
  std::uint64_t output_resource_handle;
  std::uint64_t submission_serial;
  std::uint64_t completion_value;
  std::uint64_t gpu_timestamp_begin_ns;
  std::uint64_t gpu_timestamp_end_ns;
  std::uint64_t output_digest;
  std::uint32_t submitted;
  std::uint32_t completed;
  std::uint32_t output_written;
  std::uint32_t cpu_fallback_observed;
  std::uint32_t backend_error_count;
} DigitorGpuExecutionProof;

typedef struct DigitorGpuProofResult {
  std::uint32_t status;
  std::uint64_t proof_digest;
  std::uint64_t gpu_duration_ns;
} DigitorGpuProofResult;

std::uint32_t digitor_validate_gpu_execution_proof(
    const DigitorGpuExecutionProof* proof,
    DigitorGpuProofResult* result);

}  // extern "C"
