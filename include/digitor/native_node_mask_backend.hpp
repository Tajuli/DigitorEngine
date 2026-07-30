#pragma once

#include "digitor/gpu_matte.hpp"
#include "digitor/production_node_graph.hpp"
#include "gpu/gpu_source.hpp"

#include <cstdint>
#include <span>

namespace digitor {

// Strict backend contract for node-local matte execution. Implementations must
// return backend-owned R32F resources and must never read an intermediate frame
// back to the CPU or silently fall back to a reference path.
class NativeNodeMaskBackend {
public:
  virtual ~NativeNodeMaskBackend() = default;

  [[nodiscard]] virtual NativeNodeMaskCapabilities
  native_node_mask_capabilities() const noexcept = 0;

  virtual DigitorResult generate_hsl_matte(
      const GpuSourceResource& source,
      std::int64_t timestamp,
      const HslQualifierParameters& parameters,
      GpuMatteResourcePtr& output) noexcept = 0;

  virtual DigitorResult generate_power_window_matte(
      std::uint32_t width,
      std::uint32_t height,
      std::int64_t timestamp,
      const PowerWindowSettings& settings,
      GpuMatteResourcePtr& output) noexcept = 0;

  virtual DigitorResult multiply_mattes(
      std::span<const GpuMatteResourcePtr> inputs,
      std::int64_t timestamp,
      GpuMatteResourcePtr& output) noexcept = 0;

  virtual DigitorResult composite_with_matte(
      const GpuSourceResource& original,
      const GpuSourceResource& processed,
      const GpuMatteResourcePtr& matte,
      std::int64_t timestamp,
      ProcessedGpuFramePtr& output) noexcept = 0;
};

[[nodiscard]] inline bool native_node_mask_stack_complete(
    const NativeNodeMaskCapabilities& capabilities) noexcept {
  return capabilities.hsl_matte && capabilities.power_window_matte &&
         capabilities.matte_multiply && capabilities.masked_composite;
}

} // namespace digitor
