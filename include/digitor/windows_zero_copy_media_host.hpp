#pragma once

#include "digitor/ffmpeg_d3d11va_zero_copy_decoder.hpp"
#include "digitor/windows_zero_copy_qualification.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace digitor {

struct WindowsZeroCopyMediaHostOptions {
  std::string media_path;
  std::string report_path;
  std::uint32_t max_frames{600};
  bool require_nv12{};
  bool require_p010{};
  bool strict_gpu_first{true};
  // Qualification-only. Enables guarded D3D12 readback and an independent
  // FFmpeg software reference decoder. Never enable for preview/export.
  bool complete_validation{};
};

struct WindowsZeroCopyMediaHostInfo {
  std::string codec;
  std::string pixel_format;
  std::uint32_t width{};
  std::uint32_t height{};
  double frame_rate{};
  std::uint64_t decoded_frames{};
  bool hardware_decode{};
  bool d3d11va_surface{};
  bool complete_validation{};
};

class WindowsZeroCopyMediaHost final {
public:
  WindowsZeroCopyMediaHost(void* d3d12_device,
                           WindowsZeroCopyMediaHostOptions options);
  ~WindowsZeroCopyMediaHost();

  WindowsZeroCopyMediaHost(const WindowsZeroCopyMediaHost&) = delete;
  WindowsZeroCopyMediaHost& operator=(const WindowsZeroCopyMediaHost&) = delete;

  [[nodiscard]] DigitorResult open(std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] DigitorResult qualify(
      const WindowsZeroCopyThresholds&,
      WindowsZeroCopyQualificationReport&,
      std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] const WindowsZeroCopyMediaHostInfo& info() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace digitor
