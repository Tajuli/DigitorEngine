#pragma once

#include "digitor/windows_zero_copy_import.hpp"

#include <cstdint>
#include <string>

namespace digitor {

// Additive FFmpeg bridge. The AVFrame remains opaque in the public API so
// clients that do not build with FFmpeg do not inherit FFmpeg headers.
struct FfmpegD3D11vaExtractionResult {
  WindowsZeroCopySurface surface;
  bool frame_is_d3d11{};
  bool texture_retained{};
  bool shareable_texture_reused{};
  bool shareable_copy_created{};
  bool shared_handle_created{};
  bool no_cpu_transfer{};
  std::string diagnostic;
};

// Extracts an FFmpeg AV_PIX_FMT_D3D11 frame into the existing Windows zero-copy
// surface contract. `av_frame` must point to AVFrame. `timestamp_us` must already
// be rescaled from the stream time base to microseconds by the decoder. The
// returned lifetime owns an av_frame_ref plus all COM/shared-handle resources.
// No av_hwframe_transfer_data or swscale operation is permitted.
[[nodiscard]] DigitorResult extract_ffmpeg_d3d11va_surface(
    void* av_frame,
    std::int64_t timestamp_us,
    FfmpegD3D11vaExtractionResult& out) noexcept;

} // namespace digitor
