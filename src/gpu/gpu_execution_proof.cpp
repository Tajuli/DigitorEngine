#include "digitor/gpu_execution_proof.hpp"

#include <cstddef>

namespace digitor {
namespace {

std::uint64_t append_hash(std::uint64_t hash, const void* data,
                          std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash;
}

bool backend_supported(GpuProofPlatform platform,
                       GpuProofBackend backend) noexcept {
  switch (platform) {
    case GpuProofPlatform::windows:
      return backend == GpuProofBackend::vulkan ||
             backend == GpuProofBackend::d3d12;
    case GpuProofPlatform::android:
      return backend == GpuProofBackend::vulkan ||
             backend == GpuProofBackend::gles;
    case GpuProofPlatform::macos:
    case GpuProofPlatform::ios:
      return backend == GpuProofBackend::metal;
    case GpuProofPlatform::unknown:
      return false;
  }
  return false;
}

}  // namespace

std::uint64_t gpu_execution_proof_digest(
    const GpuExecutionProof& proof) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append_hash(hash, &proof.platform, sizeof(proof.platform));
  hash = append_hash(hash, &proof.backend, sizeof(proof.backend));
  hash = append_hash(hash, &proof.command_buffer_handle,
                     sizeof(proof.command_buffer_handle));
  hash = append_hash(hash, &proof.queue_handle, sizeof(proof.queue_handle));
  hash = append_hash(hash, &proof.input_resource_handle,
                     sizeof(proof.input_resource_handle));
  hash = append_hash(hash, &proof.output_resource_handle,
                     sizeof(proof.output_resource_handle));
  hash = append_hash(hash, &proof.submission_serial,
                     sizeof(proof.submission_serial));
  hash = append_hash(hash, &proof.completion_value,
                     sizeof(proof.completion_value));
  hash = append_hash(hash, &proof.gpu_timestamp_begin_ns,
                     sizeof(proof.gpu_timestamp_begin_ns));
  hash = append_hash(hash, &proof.gpu_timestamp_end_ns,
                     sizeof(proof.gpu_timestamp_end_ns));
  hash = append_hash(hash, &proof.output_digest, sizeof(proof.output_digest));
  hash = append_hash(hash, &proof.submitted, sizeof(proof.submitted));
  hash = append_hash(hash, &proof.completed, sizeof(proof.completed));
  hash = append_hash(hash, &proof.output_written, sizeof(proof.output_written));
  hash = append_hash(hash, &proof.cpu_fallback_observed,
                     sizeof(proof.cpu_fallback_observed));
  hash = append_hash(hash, &proof.backend_error_count,
                     sizeof(proof.backend_error_count));
  return hash;
}

GpuProofResult validate_gpu_execution_proof(
    const GpuExecutionProof& proof) noexcept {
  GpuProofResult result;
  if (!backend_supported(proof.platform, proof.backend) ||
      proof.backend == GpuProofBackend::cpu) {
    result.status = GpuProofStatus::backend_mismatch;
    return result;
  }
  if (proof.submitted == 0u || proof.submission_serial == 0u) {
    result.status = GpuProofStatus::not_submitted;
    return result;
  }
  if (proof.completed == 0u || proof.completion_value == 0u) {
    result.status = GpuProofStatus::not_completed;
    return result;
  }
  if (proof.command_buffer_handle == 0u || proof.queue_handle == 0u ||
      proof.input_resource_handle == 0u || proof.output_resource_handle == 0u ||
      proof.input_resource_handle == proof.output_resource_handle) {
    result.status = GpuProofStatus::missing_resource;
    return result;
  }
  if (proof.gpu_timestamp_begin_ns == 0u ||
      proof.gpu_timestamp_end_ns <= proof.gpu_timestamp_begin_ns) {
    result.status = GpuProofStatus::missing_timing;
    return result;
  }
  if (proof.output_written == 0u || proof.output_digest == 0u) {
    result.status = GpuProofStatus::output_unverified;
    return result;
  }
  if (proof.cpu_fallback_observed != 0u) {
    result.status = GpuProofStatus::cpu_fallback_detected;
    return result;
  }
  if (proof.backend_error_count != 0u) {
    result.status = GpuProofStatus::backend_error;
    return result;
  }
  result.status = GpuProofStatus::qualified;
  result.proof_digest = gpu_execution_proof_digest(proof);
  result.gpu_duration_ns =
      proof.gpu_timestamp_end_ns - proof.gpu_timestamp_begin_ns;
  return result;
}

GpuProofResult GpuProofSequence::validate(
    const GpuExecutionProof& proof) noexcept {
  GpuProofResult result = validate_gpu_execution_proof(proof);
  if (result.status != GpuProofStatus::qualified) {
    return result;
  }
  if (proof.submission_serial <= last_submission_serial_ ||
      proof.completion_value <= last_completion_value_) {
    result.status = GpuProofStatus::replay_detected;
    result.proof_digest = 0u;
    result.gpu_duration_ns = 0u;
    return result;
  }
  last_submission_serial_ = proof.submission_serial;
  last_completion_value_ = proof.completion_value;
  return result;
}

void GpuProofSequence::reset() noexcept {
  last_submission_serial_ = 0u;
  last_completion_value_ = 0u;
}

}  // namespace digitor
