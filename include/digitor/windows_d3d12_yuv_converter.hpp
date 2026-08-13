#pragma once

#include "digitor/windows_zero_copy_import.hpp"

#include <functional>
#include <memory>

namespace digitor {

using WindowsD3D12ConvertedFrameFactory = std::function<ProcessedGpuFramePtr(
    void* input_resource, void* output_resource,
    const WindowsZeroCopySurface&, DigitorPixelFormat output_format,
    std::uint64_t identity)>;

// Isolated opt-in D3D12 converter. It performs no CPU pixel transfer: the
// imported NV12/P010 resource is sampled as two plane SRVs and converted into
// a floating-point RGBA UAV by the shared per-pixel kernel used by preview and
// export.
class WindowsD3D12YuvConverter final {
public:
  explicit WindowsD3D12YuvConverter(
      void* d3d12_device, const void* frame_context_identity = nullptr,
      DigitorPixelFormat output_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT,
      WindowsD3D12ConvertedFrameFactory frame_factory = {});
  ~WindowsD3D12YuvConverter();

  WindowsD3D12YuvConverter(const WindowsD3D12YuvConverter&) = delete;
  WindowsD3D12YuvConverter& operator=(const WindowsD3D12YuvConverter&) = delete;

  [[nodiscard]] WindowsD3D12ConvertCallback callback();
  [[nodiscard]] DigitorResult convert(void* imported_resource,
                                      const WindowsZeroCopySurface&,
                                      ProcessedGpuFramePtr&) noexcept;

private:
  struct Impl;
  explicit WindowsD3D12YuvConverter(std::shared_ptr<Impl> initialized) noexcept
      : impl_(std::move(initialized)) {}
  std::shared_ptr<Impl> impl_;
};

} // namespace digitor
