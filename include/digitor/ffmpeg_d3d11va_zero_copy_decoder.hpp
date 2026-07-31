#pragma once

#include "digitor/ffmpeg_d3d11va_surface.hpp"
#include "digitor/windows_d3d12_yuv_converter.hpp"
#include "digitor/windows_zero_copy_import.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

enum class ZeroCopyFallbackPolicy : std::uint32_t {
  forbid_cpu_transfer = 0,
  allow_explicit_legacy_fallback = 1
};

struct FfmpegD3D11vaZeroCopyOptions {
  ZeroCopyFallbackPolicy fallback{ZeroCopyFallbackPolicy::forbid_cpu_transfer};
  bool require_p010_for_10bit{true};
};

struct FfmpegD3D11vaZeroCopyResult {
  ProcessedGpuFramePtr frame;
  FfmpegD3D11vaExtractionResult extraction;
  WindowsZeroCopyQualification import;
  bool zero_copy_attempted{};
  bool zero_copy_succeeded{};
  bool legacy_fallback_allowed{};
  bool legacy_fallback_requested{};
  std::string diagnostic;
};

using LegacyCpuFallbackCallback = std::function<DigitorResult(
    void* av_frame, std::int64_t timestamp_us, ProcessedGpuFramePtr&)>;

// Additive orchestration wrapper. It never calls av_hwframe_transfer_data.
// When policy explicitly allows fallback, the caller-provided legacy callback
// may do so outside this module. The default policy is strict GPU-first.
class FfmpegD3D11vaZeroCopyDecoder final {
public:
  FfmpegD3D11vaZeroCopyDecoder(void* d3d12_device,
                               FfmpegD3D11vaZeroCopyOptions options = {},
                               LegacyCpuFallbackCallback legacy = {});
  ~FfmpegD3D11vaZeroCopyDecoder();

  FfmpegD3D11vaZeroCopyDecoder(const FfmpegD3D11vaZeroCopyDecoder&) = delete;
  FfmpegD3D11vaZeroCopyDecoder& operator=(const FfmpegD3D11vaZeroCopyDecoder&) = delete;

  [[nodiscard]] DigitorResult process(void* av_frame,
                                      std::int64_t timestamp_us,
                                      FfmpegD3D11vaZeroCopyResult&) noexcept;

private:
  FfmpegD3D11vaZeroCopyOptions options_;
  LegacyCpuFallbackCallback legacy_;
  std::unique_ptr<WindowsD3D12YuvConverter> converter_;
  std::unique_ptr<WindowsD3D12ZeroCopyImporter> importer_;
};

} // namespace digitor
