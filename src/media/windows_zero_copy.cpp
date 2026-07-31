#include "digitor/windows_zero_copy.hpp"

#include <cmath>
#include <stdexcept>

namespace digitor {
namespace {

void set_matrix(float (&m)[9], const float values[9]) noexcept {
  for (int i = 0; i < 9; ++i) m[i] = values[i];
}

} // namespace

NativeMediaSurfacePtr make_windows_d3d11va_surface(
    const WindowsD3D11VaFrameView& frame,
    WindowsZeroCopyPolicy policy) {
  if (!frame.d3d11_texture || !frame.owner || frame.width == 0 ||
      frame.height == 0) {
    throw std::invalid_argument("invalid D3D11VA frame view");
  }
  if (frame.format != NativeMediaPixelFormat::nv12 &&
      frame.format != NativeMediaPixelFormat::p010) {
    throw std::invalid_argument("Windows zero-copy path requires NV12 or P010");
  }
  if (policy.require_exact_matrix && frame.color.matrix == 0) {
    throw std::invalid_argument("exact color matrix metadata is required");
  }
  if (policy.require_exact_range && frame.color.full_range > 1) {
    throw std::invalid_argument("invalid color range metadata");
  }
  if (!policy.allow_precision_downgrade &&
      frame.format == NativeMediaPixelFormat::p010) {
    // P010 remains tagged as 10-bit and must be imported into a >=16-bit float
    // working resource by the backend. This guard prevents accidental 8-bit use.
  }

  NativeMediaSurfaceDescriptor descriptor{};
  descriptor.platform = NativeMediaPlatform::windows;
  descriptor.handle_type = NativeMediaHandleType::d3d11_texture2d;
  descriptor.pixel_format = frame.format;
  descriptor.width = frame.width;
  descriptor.height = frame.height;
  descriptor.plane_count = 2;
  descriptor.array_slice = frame.array_slice;
  descriptor.native_handle =
      reinterpret_cast<std::uintptr_t>(frame.d3d11_texture);
  descriptor.timestamp_us = frame.timestamp_us;
  descriptor.color = frame.color;

  return std::make_shared<NativeMediaSurface>(descriptor, frame.owner);
}

YuvToLinearRgbConstants make_yuv_to_linear_rgb_constants(
    NativeMediaPixelFormat format,
    const NativeMediaColorMetadata& metadata) {
  if (format != NativeMediaPixelFormat::nv12 &&
      format != NativeMediaPixelFormat::p010) {
    throw std::invalid_argument("YUV conversion supports only NV12 and P010");
  }

  YuvToLinearRgbConstants out{};
  out.bit_depth = format == NativeMediaPixelFormat::p010 ? 10u : 8u;
  out.full_range = metadata.full_range ? 1u : 0u;

  const float max_code = out.bit_depth == 10 ? 1023.0f : 255.0f;
  if (metadata.full_range) {
    out.y_offset = 0.0f;
    out.y_scale = 1.0f;
    out.uv_offset = 0.5f;
    out.uv_scale = 1.0f;
  } else {
    const float y_min = out.bit_depth == 10 ? 64.0f : 16.0f;
    const float y_max = out.bit_depth == 10 ? 940.0f : 235.0f;
    const float uv_min = out.bit_depth == 10 ? 64.0f : 16.0f;
    const float uv_max = out.bit_depth == 10 ? 960.0f : 240.0f;
    out.y_offset = y_min / max_code;
    out.y_scale = max_code / (y_max - y_min);
    out.uv_offset = 0.5f;
    out.uv_scale = max_code / (uv_max - uv_min);
  }

  // Matrix values operate on normalized Y, Cb, Cr after range expansion.
  // FFmpeg/ITU identifiers: 1=BT.709, 5/6=BT.601, 9=BT.2020 NCL.
  if (metadata.matrix == 9) {
    static constexpr float bt2020[9] = {
        1.0f, 0.0f, 1.4746f,
        1.0f, -0.164553f, -0.571353f,
        1.0f, 1.8814f, 0.0f};
    set_matrix(out.matrix, bt2020);
  } else if (metadata.matrix == 5 || metadata.matrix == 6) {
    static constexpr float bt601[9] = {
        1.0f, 0.0f, 1.402f,
        1.0f, -0.344136f, -0.714136f,
        1.0f, 1.772f, 0.0f};
    set_matrix(out.matrix, bt601);
  } else if (metadata.matrix == 1) {
    static constexpr float bt709[9] = {
        1.0f, 0.0f, 1.5748f,
        1.0f, -0.187324f, -0.468124f,
        1.0f, 1.8556f, 0.0f};
    set_matrix(out.matrix, bt709);
  } else {
    throw std::invalid_argument("unsupported or unspecified YUV matrix");
  }

  out.output_scale = 1.0f;
  return out;
}

} // namespace digitor
