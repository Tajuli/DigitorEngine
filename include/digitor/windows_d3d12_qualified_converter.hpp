#pragma once

#include "digitor/windows_zero_copy_import.hpp"

#include <memory>

namespace digitor {

class WindowsD3D12QualifiedConverter final {
public:
  explicit WindowsD3D12QualifiedConverter(void* d3d12_device);
  ~WindowsD3D12QualifiedConverter();

  WindowsD3D12QualifiedConverter(const WindowsD3D12QualifiedConverter&) = delete;
  WindowsD3D12QualifiedConverter& operator=(const WindowsD3D12QualifiedConverter&) = delete;

  [[nodiscard]] WindowsD3D12ConvertCallback callback();
  [[nodiscard]] DigitorResult convert(void* imported_resource,
                                      const WindowsZeroCopySurface&,
                                      ProcessedGpuFramePtr&) noexcept;
private:
  struct Impl;
  explicit WindowsD3D12QualifiedConverter(std::shared_ptr<Impl> impl)
      : impl_(std::move(impl)) {}
  std::shared_ptr<Impl> impl_;
};

} // namespace digitor
