#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "digitor/color.hpp"
#include "digitor/digitor.h"
#include "digitor/rgb_curves.hpp"
#include "digitor/primary_wheels.hpp"
#include "digitor/log_wheels.hpp"
#include "digitor/qualifier.hpp"
#include "digitor/production_node_graph.hpp"
#include "gpu/execution_provenance.hpp"
#include "digitor/gpu_frame.hpp"
#include "gpu/gpu_source.hpp"
#include "gpu/preview_consumer.hpp"
#include "gpu/native_pipeline_cache.hpp"
#include "platform/platform.hpp"

namespace digitor {

class IRenderBackend {
public:
  IRenderBackend();
  virtual ~IRenderBackend();

  virtual bool initialize(bool enable_validation) = 0;
  virtual void shutdown() noexcept = 0;
  [[nodiscard]] virtual DigitorRendererInfo info() const noexcept = 0;
  virtual DigitorResult create_texture(const DigitorTextureDesc &,
                                       void **out) noexcept;
  virtual DigitorResult create_buffer(const DigitorBufferDesc &,
                                      void **out) noexcept;
  virtual DigitorResult create_sampler(const DigitorSamplerDesc &,
                                       void **out) noexcept;
  virtual DigitorResult map_buffer(void *, uint64_t offset, uint64_t size,
                                   void **out) noexcept;
  virtual void unmap_buffer(void *) noexcept;
  virtual void destroy_texture(void *) noexcept;
  virtual void destroy_buffer(void *) noexcept;
  virtual void destroy_sampler(void *) noexcept;

  // Records and submits the backend's native clear/copy pass, then reads the
  // RGBA8 render target back for preview.  Keeping this operation internal
  // preserves the v2 C ABI while allowing preview to consume GPU pixels.
  virtual DigitorResult
  render_rgba8(uint32_t width, uint32_t height, std::span<const uint8_t> source,
               std::vector<uint8_t> &destination) noexcept;
  virtual DigitorResult grade_rgba32f(std::span<const Color> source,
                                      std::span<Color> destination,
                                      const ColorGrade &parameters) noexcept;
  DigitorResult curves_rgba32f(std::span<const Color> source,
                               std::span<Color> destination,
                               const CompiledRgbCurves&) noexcept;
  // Live preview contract. Unlike curves_rgba32f (validation/export), this
  // function cannot return CPU pixels and has no readback fallback.
  DigitorResult process_curves_gpu(std::span<const Color> source,
                                   std::uint32_t width, std::uint32_t height,
                                   std::int64_t timestamp,
                                   const CompiledRgbCurves&,
                                   ProcessedGpuFramePtr& out) noexcept;
  DigitorResult process_primary_wheels_gpu(std::span<const Color>,std::uint32_t,std::uint32_t,
      std::int64_t,const PrimaryWheelsParameters&,ProcessedGpuFramePtr&) noexcept;
  [[nodiscard]] GpuSourceResource gpu_source(const ProcessedGpuFramePtr&) const noexcept;
  DigitorResult process_curves_gpu(const GpuSourceResource&,std::int64_t,const CompiledRgbCurves&,ProcessedGpuFramePtr&) noexcept;
  DigitorResult process_primary_wheels_gpu(const GpuSourceResource&,std::int64_t,const PrimaryWheelsParameters&,ProcessedGpuFramePtr&) noexcept;
  DigitorResult process_log_wheels_gpu(std::span<const Color>,std::uint32_t,std::uint32_t,std::int64_t,const LogWheelsParameters&,ProcessedGpuFramePtr&) noexcept;
  DigitorResult process_log_wheels_gpu(const GpuSourceResource&,std::int64_t,const LogWheelsParameters&,ProcessedGpuFramePtr&) noexcept;
  DigitorResult validation_readback_log_wheels(const ProcessedGpuFramePtr&,std::span<Color>) noexcept;
  DigitorResult process_hsl_qualifier_gpu(std::span<const Color>,std::uint32_t,std::uint32_t,std::int64_t,const HslQualifierParameters&,ProcessedGpuFramePtr&) noexcept;
  DigitorResult process_hsl_qualifier_gpu(const GpuSourceResource&,std::int64_t,const HslQualifierParameters&,ProcessedGpuFramePtr&) noexcept;
  // Generic backend-native selected-node pass. Backends override this for
  // LUT/effects/window/masked composition without exposing platform resources.
  DigitorResult process_node_operation_gpu(const GpuSourceResource&, std::int64_t,
      const NodeOperation&, ProcessedGpuFramePtr&) noexcept;
  // Backend-native parallel mixer. Inputs must belong to this backend/context.
  DigitorResult mix_gpu_sources(std::span<const GpuSourceResource>, std::int64_t,
      ProcessedGpuFramePtr&) noexcept;
  DigitorResult validation_readback_hsl_qualifier(const ProcessedGpuFramePtr&,std::span<float>) noexcept;
  DigitorResult validation_readback_primary_wheels(const ProcessedGpuFramePtr&,
      std::span<Color>) noexcept;
  // Operation-independent final-frame readback contract used by export and
  // validation. Backends may override the protected generic seam later; the
  // default routes through their established RGBA32F readback implementation.
  DigitorResult validation_readback_final_frame(const ProcessedGpuFramePtr&,
      std::span<Color>) noexcept;
  DigitorResult present_gpu_frame(const ProcessedGpuFramePtr&) noexcept;
  DigitorResult create_preview_consumer(const ProcessedGpuFramePtr&,
      std::shared_ptr<PreviewConsumerDestination>&) noexcept;
  [[nodiscard]] const ExecutionProvenance &execution_provenance() const noexcept {
    return provenance_;
  }
  [[nodiscard]] virtual NativePipelineCacheCounters native_pipeline_cache_counters() const noexcept { return {}; }
  [[nodiscard]] virtual NativeResourceCounts native_resource_counts() const noexcept { return {}; }
  [[nodiscard]] virtual std::size_t native_pipeline_cache_size() const noexcept { return 0; }
  virtual void clear_native_pipeline_cache_for_test() noexcept {}
  [[nodiscard]] virtual bool supports_native_node_operation(NodeOperationKind kind) const noexcept;
  [[nodiscard]] virtual bool supports_native_node_mixer() const noexcept { return false; }
protected:
  static const ProcessedGpuFrame::NativeOwner& native_owner(
      const ProcessedGpuFrame& frame) noexcept { return *frame.native_holder_; }
  virtual DigitorResult execute_curves_rgba32f(std::span<const Color> source,
                                                std::span<Color> destination,
                                                const CompiledRgbCurves&) noexcept;
  virtual DigitorResult execute_process_curves_gpu(
      std::span<const Color>, std::uint32_t, std::uint32_t, std::int64_t,
      const CompiledRgbCurves&, ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_process_primary_wheels_gpu(std::span<const Color>,std::uint32_t,
      std::uint32_t,std::int64_t,const PrimaryWheelsParameters&,ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_process_curves_gpu(const GpuSourceResource&,std::int64_t,const CompiledRgbCurves&,ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_process_primary_wheels_gpu(const GpuSourceResource&,std::int64_t,const PrimaryWheelsParameters&,ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_validation_readback_primary_wheels(
      const ProcessedGpuFramePtr&,std::span<Color>) noexcept;
  virtual DigitorResult execute_process_log_wheels_gpu(std::span<const Color>,std::uint32_t,std::uint32_t,std::int64_t,const LogWheelsParameters&,ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_process_log_wheels_gpu(const GpuSourceResource&,std::int64_t,const LogWheelsParameters&,ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_validation_readback_log_wheels(const ProcessedGpuFramePtr&,std::span<Color>) noexcept;
  virtual DigitorResult execute_process_hsl_qualifier_gpu(std::span<const Color>,std::uint32_t,std::uint32_t,std::int64_t,const HslQualifierParameters&,ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_process_hsl_qualifier_gpu(const GpuSourceResource&,std::int64_t,const HslQualifierParameters&,ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_process_node_operation_gpu(const GpuSourceResource&,
      std::int64_t,const NodeOperation&,ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_mix_gpu_sources(std::span<const GpuSourceResource>,
      std::int64_t,ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_validation_readback_hsl_qualifier(const ProcessedGpuFramePtr&,std::span<float>) noexcept;
  virtual DigitorResult execute_present_gpu_frame(const ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_create_preview_consumer(const ProcessedGpuFramePtr&,
      std::shared_ptr<PreviewConsumerDestination>&) noexcept;
  void begin_grade_provenance(DigitorRendererBackend backend, bool gpu,
                              const char *device, const char *compiler,
                              const char *shader, const char *pipeline) noexcept;
  DigitorResult injected_failure(GpuFailurePoint point) noexcept;
  // Must be called immediately before the native operation named by
  // `operation`.  It is the sole stage-reached evidence seam.
  DigitorResult inject_at(GpuFailurePoint point, const char* operation) noexcept;
  ExecutionProvenance provenance_{};
private:
  std::shared_ptr<GpuContextLifetime> context_lifetime_;
  std::uint64_t context_identity_{};
};

[[nodiscard]] bool gpu_validation_requested() noexcept;

std::unique_ptr<IRenderBackend>
create_gpu_backend(DigitorRendererBackend preferred);

// Implemented by the platform-specific translation unit (or the portable stub).
std::unique_ptr<IRenderBackend>
create_native_backend(DigitorRendererBackend backend);

using BackendFactory =
    std::function<std::unique_ptr<IRenderBackend>(DigitorRendererBackend)>;

// Internal selection seam used to test platform policy without requiring GPU
// hardware.
std::unique_ptr<IRenderBackend>
select_gpu_backend(HostPlatform platform, DigitorRendererBackend preferred,
                   const BackendFactory &factory);

} // namespace digitor
