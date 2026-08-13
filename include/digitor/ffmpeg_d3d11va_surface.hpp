#pragma once

#include "digitor/windows_zero_copy_import.hpp"

#include <cstdint>
#include <string>

#if defined(_WIN32)
#include <d3d11.h>
#endif

namespace digitor {

#if defined(_WIN32)
// Constructs the deliberately small descriptor used to detach one FFmpeg
// decoder-array slice.  Kept public for Windows GPU qualification; no decoder
// resource flags are inherited by this normalized interop resource.
[[nodiscard]] D3D11_TEXTURE2D_DESC
normalized_d3d11va_interop_desc(const D3D11_TEXTURE2D_DESC &source) noexcept;

// Formats stable, actionable CreateTexture2D failure context without exposing
// COM pointer values. `debug_message` may be empty when no debug layer exists.
[[nodiscard]] std::string format_d3d11_texture_creation_failure(
    HRESULT result, const D3D11_TEXTURE2D_DESC &source,
    const D3D11_TEXTURE2D_DESC &destination, D3D_FEATURE_LEVEL feature_level,
    HRESULT format_support_result, UINT format_support,
    const std::string &debug_message = {});
#endif

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
[[nodiscard]] DigitorResult
extract_ffmpeg_d3d11va_surface(void *av_frame,
                               FfmpegD3D11vaExtractionResult &out) noexcept;

// Production-facing overload. `timestamp_us` must already be rescaled from the
// stream time base to microseconds by the decoder. Both the Windows descriptor
// and the retained NativeMediaSurface receive the exact same engine timestamp.
[[nodiscard]] DigitorResult
extract_ffmpeg_d3d11va_surface(void *av_frame, std::int64_t timestamp_us,
                               FfmpegD3D11vaExtractionResult &out) noexcept;

} // namespace digitor
