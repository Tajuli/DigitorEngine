#include "digitor/gpu_execution_proof.hpp"

int main() {
  using namespace digitor;

  GpuExecutionProof proof;
  proof.platform = GpuProofPlatform::windows;
  proof.backend = GpuProofBackend::d3d12;
  proof.command_buffer_handle = 11u;
  proof.queue_handle = 12u;
  proof.input_resource_handle = 13u;
  proof.output_resource_handle = 14u;
  proof.submission_serial = 1u;
  proof.completion_value = 1u;
  proof.gpu_timestamp_begin_ns = 100u;
  proof.gpu_timestamp_end_ns = 180u;
  proof.output_digest = 0x1234u;
  proof.submitted = 1u;
  proof.completed = 1u;
  proof.output_written = 1u;

  const auto qualified = validate_gpu_execution_proof(proof);
  if (qualified.status != GpuProofStatus::qualified ||
      qualified.proof_digest == 0u || qualified.gpu_duration_ns != 80u) {
    return 1;
  }

  auto invalid_backend = proof;
  invalid_backend.platform = GpuProofPlatform::android;
  if (validate_gpu_execution_proof(invalid_backend).status !=
      GpuProofStatus::backend_mismatch) {
    return 2;
  }

  auto fake_submission = proof;
  fake_submission.submitted = 0u;
  if (validate_gpu_execution_proof(fake_submission).status !=
      GpuProofStatus::not_submitted) {
    return 3;
  }

  auto no_timestamp = proof;
  no_timestamp.gpu_timestamp_end_ns = no_timestamp.gpu_timestamp_begin_ns;
  if (validate_gpu_execution_proof(no_timestamp).status !=
      GpuProofStatus::missing_timing) {
    return 4;
  }

  auto cpu_fallback = proof;
  cpu_fallback.cpu_fallback_observed = 1u;
  if (validate_gpu_execution_proof(cpu_fallback).status !=
      GpuProofStatus::cpu_fallback_detected) {
    return 5;
  }

  auto no_output = proof;
  no_output.output_digest = 0u;
  if (validate_gpu_execution_proof(no_output).status !=
      GpuProofStatus::output_unverified) {
    return 6;
  }

  GpuProofSequence sequence;
  if (sequence.validate(proof).status != GpuProofStatus::qualified) {
    return 7;
  }
  if (sequence.validate(proof).status != GpuProofStatus::replay_detected) {
    return 8;
  }
  proof.submission_serial = 2u;
  proof.completion_value = 2u;
  if (sequence.validate(proof).status != GpuProofStatus::qualified) {
    return 9;
  }

  DigitorGpuExecutionProof c_proof{
      static_cast<std::uint32_t>(proof.platform),
      static_cast<std::uint32_t>(proof.backend),
      proof.command_buffer_handle,
      proof.queue_handle,
      proof.input_resource_handle,
      proof.output_resource_handle,
      proof.submission_serial,
      proof.completion_value,
      proof.gpu_timestamp_begin_ns,
      proof.gpu_timestamp_end_ns,
      proof.output_digest,
      proof.submitted,
      proof.completed,
      proof.output_written,
      proof.cpu_fallback_observed,
      proof.backend_error_count,
  };
  DigitorGpuProofResult c_result{};
  if (digitor_validate_gpu_execution_proof(&c_proof, &c_result) != 0u ||
      c_result.status != static_cast<std::uint32_t>(GpuProofStatus::qualified) ||
      c_result.proof_digest == 0u) {
    return 10;
  }

  return 0;
}
