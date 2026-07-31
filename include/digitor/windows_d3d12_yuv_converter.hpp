#pragma once

#include "digitor/windows_zero_copy_import.hpp"

#include <memory>

namespace digitor {

// Isolated opt-in D3D12 converter. It performs no CPU pixel transfer: the
// imported NV12/P010 resource is sampled as two plane SRVs and converted into
// an RGBA16F UAV by the shared per-pixel kernel used by preview and export.
class WindowsD3D12YuvConverter final {
public:
  explicit WindowsD3D12YuvConverter(void* d3d12_device);
  ~WindowsD3D12YuvConverter();

  WindowsD3D12YuvConverter(const WindowsD3D12YuvConverter&) = delete;
  WindowsD3D12YuvConverter& operator=(const WindowsD3D12YuvConverter&) = delete;

  [[nodiscard]] WindowsD3D12ConvertCallback callback();
  [[nodiscard]] DigitorResult convert(void* imported_resource,
                                      const WindowsZeroCopySurface&,
                                      ProcessedGpuFramePtr&) noexcept;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace digitor
