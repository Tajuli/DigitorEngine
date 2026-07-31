#pragma once

#include "digitor/digitor.h"
#include "digitor/gpu_frame.hpp"
#include "digitor/native_media.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

// Additive, opt-in Windows interop seam. Existing decoder/render paths do not
// depend on this API. A caller supplies a decoder-owned D3D11VA surface exposed
// through a DXGI shared handle; the D3D12 adapter imports and converts it on GPU.
enum class WindowsZeroCopyFormat : std::uint32_t { nv12 = 1, p010 = 2 };
enum class WindowsYuvMatrix : std::uint32_t {
  bt601 = 1,
  bt709 = 2,
  bt2020_ncl = 3
};
enum class WindowsChromaSiting : std::uint32_t {
  left = 1,
  center = 2,
  top_left = 3
};

struct WindowsZeroCopyColor {
  WindowsYuvMatrix matrix{WindowsYuvMatrix::bt709};
  WindowsChromaSiting chroma_siting{WindowsChromaSiting::left};
  bool full_range{};
  std::int32_t primaries{};
  std::int32_t transfer{};
};

struct WindowsZeroCopySurface {
  std::uint32_t struct_size{sizeof(WindowsZeroCopySurface)};
  std::uint32_t api_version{1};
  WindowsZeroCopyFormat format{WindowsZeroCopyFormat::nv12};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t array_slice{};
  std::uintptr_t shared_handle{};
  std::uintptr_t decoder_device{};
  std::int64_t timestamp_us{};
  WindowsZeroCopyColor color{};
  NativeMediaSurfacePtr lifetime;
};

struct WindowsZeroCopyQualification {
  bool descriptor_valid{};
  bool shared_resource_opened{};
  bool format_supported{};
  bool plane_views_valid{};
  bool gpu_conversion_submitted{};
  bool rgba16f_output{};
  bool decoder_lifetime_retained{};
  bool no_cpu_readback{};
  bool per_pixel_contract_preserved{};
  std::string diagnostic;
};

// Backend callback performs the native command recording. It receives the
// imported ID3D12Resource as an opaque pointer to avoid exposing D3D headers in
// the public API. Implementations must create plane SRVs and write RGBA16F.
using WindowsD3D12ConvertCallback = std::function<DigitorResult(
    void* d3d12_resource,
    const WindowsZeroCopySurface&,
    ProcessedGpuFramePtr&)>;

class WindowsD3D12ZeroCopyImporter final {
public:
  WindowsD3D12ZeroCopyImporter(void* d3d12_device,
                               WindowsD3D12ConvertCallback converter);
  ~WindowsD3D12ZeroCopyImporter();

  WindowsD3D12ZeroCopyImporter(const WindowsD3D12ZeroCopyImporter&) = delete;
  WindowsD3D12ZeroCopyImporter& operator=(
      const WindowsD3D12ZeroCopyImporter&) = delete;

  [[nodiscard]] DigitorResult import(const WindowsZeroCopySurface&,
                                     ProcessedGpuFramePtr&,
                                     WindowsZeroCopyQualification* = nullptr)
      noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool validate_windows_zero_copy_surface(
    const WindowsZeroCopySurface&, std::string* diagnostic = nullptr) noexcept;

} // namespace digitor
