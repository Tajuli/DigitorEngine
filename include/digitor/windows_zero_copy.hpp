#pragma once

#include "digitor/native_media.hpp"

#include <cstdint>
#include <memory>

namespace digitor {

// Additive Windows-only zero-copy bridge. This API does not replace or mutate
// the existing decoder path; callers opt in by passing an FFmpeg D3D11VA frame.
struct WindowsD3D11VaFrameView {
  void* d3d11_texture{};          // ID3D11Texture2D*
  std::uint32_t array_slice{};
  std::uint32_t width{};
  std::uint32_t height{};
  NativeMediaPixelFormat format{NativeMediaPixelFormat::unknown};
  std::int64_t timestamp_us{};
  NativeMediaColorMetadata color{};
  std::shared_ptr<void> owner;    // retains the referenced AVFrame/texture
};

struct WindowsZeroCopyPolicy {
  bool require_gpu{true};
  bool require_exact_matrix{true};
  bool require_exact_range{true};
  bool allow_precision_downgrade{false};
};

// Creates a decoder-owned native surface descriptor without CPU transfer.
// The returned object retains `owner` until the GPU consumer releases it.
[[nodiscard]] NativeMediaSurfacePtr make_windows_d3d11va_surface(
    const WindowsD3D11VaFrameView& frame,
    WindowsZeroCopyPolicy policy = {});

// Per-pixel conversion constants shared by preview and export. Values are
// derived from source range/matrix metadata and preserve NV12/P010 precision.
struct YuvToLinearRgbConstants {
  float y_offset{};
  float y_scale{1.0f};
  float uv_offset{0.5f};
  float uv_scale{1.0f};
  float matrix[9]{};
  float output_scale{1.0f};
  std::uint32_t bit_depth{8};
  std::uint32_t full_range{};
};

[[nodiscard]] YuvToLinearRgbConstants make_yuv_to_linear_rgb_constants(
    NativeMediaPixelFormat format,
    const NativeMediaColorMetadata& metadata);

} // namespace digitor
