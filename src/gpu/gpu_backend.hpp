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
#include "gpu/execution_provenance.hpp"
#include "digitor/gpu_frame.hpp"
#include "platform/platform.hpp"

namespace digitor {

class IRenderBackend {
public:
  virtual ~IRenderBackend() = default;

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
  DigitorResult validation_readback_primary_wheels(const ProcessedGpuFramePtr&,
      std::span<Color>) noexcept;
  DigitorResult present_gpu_frame(const ProcessedGpuFramePtr&) noexcept;
  [[nodiscard]] const ExecutionProvenance &execution_provenance() const noexcept {
    return provenance_;
  }
protected:
  static const ProcessedGpuFrame::NativeOwner& native_owner(
      const ProcessedGpuFrame& frame) noexcept { return frame.native_; }
  virtual DigitorResult execute_curves_rgba32f(std::span<const Color> source,
                                                std::span<Color> destination,
                                                const CompiledRgbCurves&) noexcept;
  virtual DigitorResult execute_process_curves_gpu(
      std::span<const Color>, std::uint32_t, std::uint32_t, std::int64_t,
      const CompiledRgbCurves&, ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_process_primary_wheels_gpu(std::span<const Color>,std::uint32_t,
      std::uint32_t,std::int64_t,const PrimaryWheelsParameters&,ProcessedGpuFramePtr&) noexcept;
  virtual DigitorResult execute_validation_readback_primary_wheels(
      const ProcessedGpuFramePtr&,std::span<Color>) noexcept;
  virtual DigitorResult execute_present_gpu_frame(const ProcessedGpuFramePtr&) noexcept;
  void begin_grade_provenance(DigitorRendererBackend backend, bool gpu,
                              const char *device, const char *compiler,
                              const char *shader, const char *pipeline) noexcept;
  DigitorResult injected_failure(GpuFailurePoint point) noexcept;
  ExecutionProvenance provenance_{};
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
