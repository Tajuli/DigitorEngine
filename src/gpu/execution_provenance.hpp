#pragma once

#include "digitor/digitor.h"

#include <cstdint>
#include <span>
#include <string>
#include "digitor/gpu_frame.hpp"
#include "gpu/native_pipeline_cache.hpp"

namespace digitor {

enum class CacheDisposition { NotApplicable, Miss, Hit };
enum class GpuFailurePoint {
  None,
  ShaderCompilation,
  LibraryCreation,
  ShaderFunctionLookup,
  VertexShaderCreation,
  VertexShaderCompilation,
  FragmentShaderCreation,
  FragmentShaderCompilation,
  ProgramCreation,
  ProgramLink,
  DescriptorSetLayoutCreation,
  RootSignatureSerialization,
  RootSignatureCreation,
  PipelineLayoutCreation,
  PipelineCreation,
  SourceResourceCreation,
  SourceMemoryAllocation,
  SourceMemoryBinding,
  OutputResourceCreation,
  OutputMemoryAllocation,
  OutputMemoryBinding,
  PreviewDestinationCreation,
  SourceResourceStorage,
  OutputResourceStorage,
  PreviewDestinationStorage,
  LutResourceCreation,
  LutUpload,
  ParameterResourceCreation,
  ParameterUpload,
  BufferMemoryAllocation,
  BufferMemoryBinding,
  DescriptorPoolCreation,
  DescriptorHeapCreation,
  DescriptorSetAllocation,
  ImageViewCreation,
  FramebufferCreation,
  FramebufferAttachment,
  FramebufferValidation,
  ShaderResourceViewCreation,
  CpuSourceShaderResourceViewCreation,
  GpuSourceShaderResourceViewCreation,
  LutShaderResourceViewCreation,
  UnorderedAccessViewCreation,
  SourceUpload,
  UniformLookup,
  ResourceBinding,
  DescriptorUpdate,
  SourceUploadRecording,
  LutUploadRecording,
  ParameterUploadRecording,
  DrawSetup,
  CommandPoolCreation,
  CommandQueueCreation,
  CommandAllocatorResetOrCreation,
  CommandBufferOrListAllocation,
  CommandBufferOrListBeginReset,
  CommandRecording,
  ComputeEncoderCreation,
  BlitEncoderCreation,
  SourceTextureBinding,
  OutputTextureBinding,
  BufferBinding,
  DispatchSetup,
  EncoderCompletion,
  SourceTransition,
  ConsumerDestinationTransition,
  ValidationTransition,
  DispatchOrDraw,
  Blit,
  ConsumerCopySubmission,
  Flush,
  CommandBufferOrListClose,
  QueueSubmission,
  FenceSignal,
  FenceCreation,
  EventCreation,
  EventSetup,
  SynchronizationWait,
  SynchronizationVerification,
  CommandStatusVerification,
  ProcessedFrameCreation,
  PreviewAcquisition,
  PreviewPresentation,
  ValidationReadbackResourceCreation,
  ValidationReadbackCopy,
  ValidationReadbackMap,
  CpuReadbackCopy,
  ValidationCpuCopy,
  DeterministicOutOfMemory,
  DeviceLost,
  // Source-compatible names retained for older internal tests.  These are not
  // part of the public C ABI and new qualification must use the exact stages.
  ReflectionValidation,
  DescriptorAllocation = DescriptorSetAllocation,
  SourceAllocation = SourceResourceCreation,
  DestinationAllocation = OutputResourceCreation,
  Upload = SourceUpload,
  Synchronization = SynchronizationWait,
  Readback = ValidationReadbackCopy,
  OutOfMemory = DeterministicOutOfMemory
};

const char* gpu_failure_point_name(GpuFailurePoint) noexcept;
std::span<const GpuFailurePoint> all_gpu_failure_points() noexcept;

struct NativeResourceCounts {
  std::int64_t images{}, memory_allocations{}, image_views{}, buffers{};
  std::int64_t descriptor_pools{}, descriptor_heaps{}, descriptor_sets{};
  std::int64_t command_resources{}, pipelines{}, consumer_destinations{};
  std::int64_t frame_owners{};
  auto operator<=>(const NativeResourceCounts&) const = default;
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
  bool curves_enabled{};
  std::uint32_t curve_lut_size{};
  std::string compiled_curve_identity;
  std::string native_lut_resource_identity;
  std::string native_curve_shader_identity;
  CacheDisposition native_lut_cache{CacheDisposition::NotApplicable};
  bool curve_source_bound{}, curve_destination_bound{}, curve_lut_bound{};
  bool curve_parameters_bound{}, curve_identity_bypassed{};
  bool validation_readback_completed{};
  PreviewSource preview_source{PreviewSource::none};
  bool direct_preview_consumed{};
  std::uint64_t cpu_curve_invocations{};
  std::uint64_t curve_fallback_invocations{};
  bool primary_wheels_enabled{};
  std::string primary_wheels_parameter_identity;
  std::string primary_wheels_shader_identity;
  std::string primary_wheels_pipeline_identity;
  bool primary_wheels_parameters_bound{};
  bool primary_wheels_source_bound{};
  bool primary_wheels_destination_bound{};
  std::uint64_t cpu_primary_wheels_invocations{};
  std::uint64_t primary_wheels_fallback_invocations{};
  std::uint64_t normal_preview_readback_count{};
  std::string failure_stage;
  GpuFailurePoint requested_failure_point{GpuFailurePoint::None};
  GpuFailurePoint actual_stage_reached{GpuFailurePoint::None};
  std::string failure_backend;
  std::string failure_operation;
  std::string failure_path;
  DigitorResult failure_result{DIGITOR_RESULT_OK};
  bool output_cleared{};
  bool cleanup_baseline{};
  bool cache_valid{};
  NativeResourceCounts resources_before{}, resources_after{};
  NativePipelineCacheCounters cache_before{}, cache_after{};
  std::uint64_t intermediate_readback_count{};
  std::uint64_t intermediate_reupload_count{};
  std::int64_t preview_acquisition_balance{};
  bool recovery_succeeded{};
};

void set_gpu_failure_point(GpuFailurePoint point) noexcept;
GpuFailurePoint gpu_failure_point() noexcept;
void note_cpu_color_reference() noexcept;
std::uint64_t cpu_color_reference_count() noexcept;
void reset_cpu_color_reference_count() noexcept;
void note_cpu_curve_reference() noexcept;
std::uint64_t cpu_curve_reference_count() noexcept;

} // namespace digitor
