#pragma once

#include "digitor/digitor.h"

#include <cstdint>
#include <string>

namespace digitor {

enum class CacheDisposition { NotApplicable, Miss, Hit };
enum class GpuFailurePoint {
  None,
  ShaderCompilation,
  ReflectionValidation,
  PipelineCreation,
  DescriptorAllocation,
  SourceAllocation,
  DestinationAllocation,
  Upload,
  CommandRecording,
  QueueSubmission,
  Synchronization,
  Readback,
  DeviceLost,
  OutOfMemory
};

// Internal test/debug evidence. Stable strings deliberately replace native
// addresses; this type is not included by, or exposed through, the public ABI.
struct ExecutionProvenance {
  DigitorRendererBackend selected_backend{DIGITOR_RENDERER_AUTO};
  bool gpu_execution{};
  std::string native_device_identity;
  std::string compiler_identity;
  std::string compiled_shader_identity;
  std::string native_pipeline_identity;
  bool source_upload_performed{};
  bool command_recorded{};
  bool dispatch_or_draw_issued{};
  bool queue_submission_issued{};
  bool synchronization_waited{};
  bool output_written{};
  bool readback_performed{};
  std::uint64_t cpu_color_reference_invocations{};
  std::uint64_t cpu_fallback_invocations{};
  std::int64_t native_error_code{};
  bool device_lost{};
  CacheDisposition shader_pipeline_cache{CacheDisposition::NotApplicable};
  CacheDisposition graph_cache{CacheDisposition::NotApplicable};
};

void set_gpu_failure_point(GpuFailurePoint point) noexcept;
GpuFailurePoint gpu_failure_point() noexcept;
void note_cpu_color_reference() noexcept;
std::uint64_t cpu_color_reference_count() noexcept;
void reset_cpu_color_reference_count() noexcept;

} // namespace digitor
