#include "digitor/gpu_execution_proof.hpp"

extern "C" std::uint32_t digitor_validate_gpu_execution_proof(
    const DigitorGpuExecutionProof* proof,
    DigitorGpuProofResult* result) {
  if (proof == nullptr || result == nullptr) {
    return 1u;
  }

  const digitor::GpuExecutionProof native{
      static_cast<digitor::GpuProofPlatform>(proof->platform),
      static_cast<digitor::GpuProofBackend>(proof->backend),
      proof->command_buffer_handle,
      proof->queue_handle,
      proof->input_resource_handle,
      proof->output_resource_handle,
      proof->submission_serial,
      proof->completion_value,
      proof->gpu_timestamp_begin_ns,
      proof->gpu_timestamp_end_ns,
      proof->output_digest,
      proof->submitted,
      proof->completed,
      proof->output_written,
      proof->cpu_fallback_observed,
      proof->backend_error_count,
  };

  const auto validated = digitor::validate_gpu_execution_proof(native);
  result->status = static_cast<std::uint32_t>(validated.status);
  result->proof_digest = validated.proof_digest;
  result->gpu_duration_ns = validated.gpu_duration_ns;
  return validated.status == digitor::GpuProofStatus::qualified ? 0u : 2u;
}
