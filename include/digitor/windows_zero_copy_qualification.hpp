#pragma once

#include "digitor/digitor.h"
#include "digitor/gpu_frame.hpp"
#include "digitor/ffmpeg_d3d11va_zero_copy_decoder.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace digitor {

struct WindowsZeroCopyThresholds {
  double max_abs_error{2.0 / 1023.0};
  double max_mean_abs_error{0.5 / 1023.0};
  double min_realtime_fps_4k{30.0};
  std::uint64_t max_live_resource_delta{};
  std::uint32_t warmup_frames{30};
  std::uint32_t measured_frames{300};
  std::uint32_t stress_iterations{2000};
};

struct WindowsZeroCopyFrameMetrics {
  std::int64_t timestamp_us{};
  double gpu_ms{};
  double max_abs_error{};
  double mean_abs_error{};
  std::uint64_t gpu_hash{};
  std::uint64_t reference_hash{};
  bool p010_precision_preserved{};
  bool metadata_match{};
};

struct WindowsZeroCopyQualificationReport {
  bool build_supported{};
  bool hardware_available{};
  bool nv12_passed{};
  bool p010_passed{};
  bool no_cpu_transfer_proven{};
  bool pixel_accuracy_passed{};
  bool preview_export_identity_passed{};
  bool stress_passed{};
  bool leak_free{};
  bool realtime_4k_passed{};
  double average_fps{};
  double p95_gpu_ms{};
  std::uint64_t resources_before{};
  std::uint64_t resources_after{};
  std::vector<WindowsZeroCopyFrameMetrics> frames;
  std::string device_name;
  std::string diagnostic;

  [[nodiscard]] bool production_ready() const noexcept {
    return build_supported && hardware_available && nv12_passed && p010_passed &&
           no_cpu_transfer_proven && pixel_accuracy_passed &&
           preview_export_identity_passed && stress_passed && leak_free &&
           realtime_4k_passed;
  }
};

using WindowsZeroCopyFrameProvider = std::function<DigitorResult(
    std::uint32_t frame_index, void*& av_frame, std::int64_t& timestamp_us)>;
using WindowsZeroCopyReferenceProvider = std::function<DigitorResult(
    std::uint32_t frame_index, std::vector<float>& rgba32f)>;
using WindowsZeroCopyReadback = std::function<DigitorResult(
    const ProcessedGpuFramePtr&, std::vector<float>& rgba32f)>;
using WindowsZeroCopyResourceCounter = std::function<std::uint64_t()>;

class WindowsZeroCopyQualificationRunner final {
public:
  WindowsZeroCopyQualificationRunner(
      FfmpegD3D11vaZeroCopyDecoder& decoder,
      WindowsZeroCopyFrameProvider frame_provider,
      WindowsZeroCopyReferenceProvider reference_provider,
      WindowsZeroCopyReadback validation_readback,
      WindowsZeroCopyResourceCounter resource_counter = {});

  [[nodiscard]] DigitorResult run(const WindowsZeroCopyThresholds&,
                                  WindowsZeroCopyQualificationReport&) noexcept;

private:
  FfmpegD3D11vaZeroCopyDecoder& decoder_;
  WindowsZeroCopyFrameProvider frame_provider_;
  WindowsZeroCopyReferenceProvider reference_provider_;
  WindowsZeroCopyReadback readback_;
  WindowsZeroCopyResourceCounter resource_counter_;
};

[[nodiscard]] std::string windows_zero_copy_report_json(
    const WindowsZeroCopyQualificationReport&);

} // namespace digitor
