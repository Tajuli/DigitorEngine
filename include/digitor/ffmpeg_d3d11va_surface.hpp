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
  bool acquire_sync_created{};
  bool no_cpu_transfer{};
  std::string diagnostic;
};

// Low-level extraction overload retained for isolated qualification. Its
// timestamp is the AVFrame native timestamp and must not be used by renderer
// frame identity code directly.
[[nodiscard]] DigitorResult extract_ffmpeg_d3d11va_surface(
    void* av_frame,
    FfmpegD3D11vaExtractionResult& out) noexcept;

// Production-facing overload. `timestamp_us` must already be rescaled from the
// stream time base to microseconds by the decoder. Both the Windows descriptor
// and the retained NativeMediaSurface receive the exact same engine timestamp.
[[nodiscard]] DigitorResult extract_ffmpeg_d3d11va_surface(
    void* av_frame,
    std::int64_t timestamp_us,
    FfmpegD3D11vaExtractionResult& out) noexcept;

} // namespace digitor
