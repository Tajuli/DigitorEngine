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
  // Decoder acquire fence exported with the D3D11VA shared surface. The D3D12
  // conversion queue must wait on this exact fence/value before sampling.
  std::uintptr_t acquire_fence_handle{};
  std::uint64_t acquire_fence_value{};
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
  // Reported by the actual engine-owned D3D12 device, without exposing Windows
  // SDK types through this public header.
  std::uint32_t shared_resource_compatibility_tier{};
  std::uint32_t shared_resource_compatibility_query_hresult{};
  std::uint32_t open_shared_handle_hresult{};
  std::uint64_t opened_width{};
  std::uint32_t opened_height{};
  std::uint32_t opened_format{};
  std::uint16_t opened_mip_levels{};
  std::uint32_t opened_sample_count{};
  std::uint32_t opened_sample_quality{};
  std::uint32_t opened_resource_flags{};
  std::string diagnostic;
};

// Backend callback performs the native command recording. It receives the
// imported ID3D12Resource as an opaque pointer to avoid exposing D3D headers in
// the public API. Implementations must create plane SRVs and write floating-point RGBA.
using WindowsD3D12ConvertCallback = std::function<DigitorResult(
    void* d3d12_resource,
    const WindowsZeroCopySurface&,
    ProcessedGpuFramePtr&)>;

class WindowsD3D12ZeroCopyImporter final {
public:
  WindowsD3D12ZeroCopyImporter(
      void* d3d12_device, WindowsD3D12ConvertCallback converter,
      DigitorPixelFormat expected_output_format =
          DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT);
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
