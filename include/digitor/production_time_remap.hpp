#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace digitor {

enum class TimeRemapInterpolation : std::uint32_t { nearest = 0, blend = 1, optical_flow = 2 };
enum class TimeRemapBackend : std::uint32_t { cpu = 0, vulkan = 1, d3d12 = 2, metal = 3, gles = 4 };
enum class TimeRemapStatus : std::uint32_t { invalid = 0, ready = 1, backend_unavailable = 2, dispatch_failed = 3 };

struct TimeRemapKeyframe {
  double timeline_seconds{};
  double source_seconds{};
  double incoming_slope{1.0};
  double outgoing_slope{1.0};
};

struct TimeRemapSettings {
  TimeRemapInterpolation interpolation{TimeRemapInterpolation::blend};
  bool preserve_audio_pitch{true};
  bool clamp_to_source{true};
  double source_duration_seconds{};
};

struct TimeRemapSample {
  double timeline_seconds{};
  double source_seconds{};
  double speed{};
  std::uint64_t source_frame_a{};
  std::uint64_t source_frame_b{};
  float blend{};
};

struct TimeRemapPlan {
  double output_fps{};
  std::vector<TimeRemapSample> samples;
  std::uint64_t digest{};
};

struct TimeRemapDispatchPacket {
  TimeRemapBackend backend{TimeRemapBackend::cpu};
  std::uint64_t command_handle{};
  std::uint64_t source_a_handle{};
  std::uint64_t source_b_handle{};
  std::uint64_t output_handle{};
  std::uint32_t width{};
  std::uint32_t height{};
  float blend{};
  TimeRemapInterpolation interpolation{TimeRemapInterpolation::blend};
};

struct TimeRemapResult {
  TimeRemapStatus status{TimeRemapStatus::invalid};
  std::uint64_t digest{};
};

using TimeRemapDispatch = std::function<bool(const TimeRemapDispatchPacket&)>;

TimeRemapPlan build_time_remap_plan(std::span<const TimeRemapKeyframe> keyframes,
                                    double output_duration_seconds,
                                    double output_fps,
                                    double source_fps,
                                    const TimeRemapSettings& settings);
TimeRemapResult dispatch_time_remap_gpu(const TimeRemapDispatchPacket& packet,
                                        const TimeRemapDispatch& dispatch);
std::uint64_t time_remap_plan_digest(const TimeRemapPlan& plan) noexcept;

}  // namespace digitor

extern "C" {
struct DigitorTimeRemapKeyframe {
  double timeline_seconds;
  double source_seconds;
  double incoming_slope;
  double outgoing_slope;
};
struct DigitorTimeRemapSettings {
  std::uint32_t interpolation;
  std::uint32_t preserve_audio_pitch;
  std::uint32_t clamp_to_source;
  double source_duration_seconds;
};
std::uint32_t digitor_time_remap_build(const DigitorTimeRemapKeyframe* keyframes,
                                       std::size_t keyframe_count,
                                       double output_duration_seconds,
                                       double output_fps,
                                       double source_fps,
                                       const DigitorTimeRemapSettings* settings,
                                       double* source_seconds_out,
                                       float* blend_out,
                                       std::size_t output_capacity,
                                       std::size_t* output_count,
                                       std::uint64_t* digest);
}
