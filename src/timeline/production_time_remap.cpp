#include "digitor/production_time_remap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace digitor {
namespace {

std::uint64_t append_hash(std::uint64_t hash, const void* data, std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint64_t>(bytes[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

bool finite_key(const TimeRemapKeyframe& key) noexcept {
  return std::isfinite(key.timeline_seconds) && std::isfinite(key.source_seconds) &&
         std::isfinite(key.incoming_slope) && std::isfinite(key.outgoing_slope);
}

bool valid_keyframes(std::span<const TimeRemapKeyframe> keys) noexcept {
  if (keys.size() < 2u) return false;
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (!finite_key(keys[i])) return false;
    if (i != 0u && keys[i].timeline_seconds <= keys[i - 1u].timeline_seconds) return false;
  }
  return true;
}

double hermite(double p0, double p1, double m0, double m1, double u, double dt) noexcept {
  const double u2 = u * u;
  const double u3 = u2 * u;
  const double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
  const double h10 = u3 - 2.0 * u2 + u;
  const double h01 = -2.0 * u3 + 3.0 * u2;
  const double h11 = u3 - u2;
  return h00 * p0 + h10 * dt * m0 + h01 * p1 + h11 * dt * m1;
}

double hermite_derivative(double p0, double p1, double m0, double m1, double u, double dt) noexcept {
  const double u2 = u * u;
  const double dh00 = 6.0 * u2 - 6.0 * u;
  const double dh10 = 3.0 * u2 - 4.0 * u + 1.0;
  const double dh01 = -6.0 * u2 + 6.0 * u;
  const double dh11 = 3.0 * u2 - 2.0 * u;
  return (dh00 * p0 + dh10 * dt * m0 + dh01 * p1 + dh11 * dt * m1) / dt;
}

}  // namespace

std::uint64_t time_remap_plan_digest(const TimeRemapPlan& plan) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append_hash(hash, &plan.output_fps, sizeof(plan.output_fps));
  if (!plan.samples.empty()) {
    hash = append_hash(hash, plan.samples.data(), plan.samples.size() * sizeof(TimeRemapSample));
  }
  return hash;
}

TimeRemapPlan build_time_remap_plan(std::span<const TimeRemapKeyframe> keyframes,
                                    double output_duration_seconds,
                                    double output_fps,
                                    double source_fps,
                                    const TimeRemapSettings& settings) {
  TimeRemapPlan plan;
  if (!valid_keyframes(keyframes) || !std::isfinite(output_duration_seconds) ||
      !std::isfinite(output_fps) || !std::isfinite(source_fps) ||
      output_duration_seconds <= 0.0 || output_fps <= 0.0 || source_fps <= 0.0 ||
      (settings.clamp_to_source && settings.source_duration_seconds <= 0.0)) {
    return plan;
  }

  const auto frame_count = static_cast<std::size_t>(std::floor(output_duration_seconds * output_fps + 0.5));
  plan.output_fps = output_fps;
  plan.samples.reserve(frame_count);

  std::size_t segment = 0u;
  for (std::size_t frame = 0; frame < frame_count; ++frame) {
    const double timeline = static_cast<double>(frame) / output_fps;
    while (segment + 1u < keyframes.size() - 1u &&
           timeline > keyframes[segment + 1u].timeline_seconds) {
      ++segment;
    }
    const auto& a = keyframes[segment];
    const auto& b = keyframes[segment + 1u];
    const double dt = b.timeline_seconds - a.timeline_seconds;
    const double u = std::clamp((timeline - a.timeline_seconds) / dt, 0.0, 1.0);
    double source = hermite(a.source_seconds, b.source_seconds, a.outgoing_slope, b.incoming_slope, u, dt);
    const double speed = hermite_derivative(a.source_seconds, b.source_seconds, a.outgoing_slope,
                                            b.incoming_slope, u, dt);
    if (settings.clamp_to_source) {
      source = std::clamp(source, 0.0, settings.source_duration_seconds);
    }
    const double source_frame = std::max(0.0, source * source_fps);
    const auto frame_a = static_cast<std::uint64_t>(std::floor(source_frame));
    auto frame_b = frame_a + 1u;
    float blend = static_cast<float>(source_frame - static_cast<double>(frame_a));
    if (settings.interpolation == TimeRemapInterpolation::nearest) {
      const auto nearest = static_cast<std::uint64_t>(std::llround(source_frame));
      frame_b = nearest;
      blend = 0.0f;
      plan.samples.push_back({timeline, source, speed, nearest, nearest, blend});
    } else {
      plan.samples.push_back({timeline, source, speed, frame_a, frame_b, blend});
    }
  }
  plan.digest = time_remap_plan_digest(plan);
  return plan;
}

TimeRemapResult dispatch_time_remap_gpu(const TimeRemapDispatchPacket& packet,
                                        const TimeRemapDispatch& dispatch) {
  TimeRemapResult result;
  if (packet.backend == TimeRemapBackend::cpu || packet.command_handle == 0u ||
      packet.source_a_handle == 0u || packet.output_handle == 0u || packet.width == 0u ||
      packet.height == 0u || !std::isfinite(packet.blend) || packet.blend < 0.0f ||
      packet.blend > 1.0f ||
      (packet.interpolation != TimeRemapInterpolation::nearest && packet.source_b_handle == 0u)) {
    return result;
  }
  if (!dispatch) {
    result.status = TimeRemapStatus::backend_unavailable;
    return result;
  }
  result.status = dispatch(packet) ? TimeRemapStatus::ready : TimeRemapStatus::dispatch_failed;
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_time_remap_build(
    const DigitorTimeRemapKeyframe* keyframes,
    std::size_t keyframe_count,
    double output_duration_seconds,
    double output_fps,
    double source_fps,
    const DigitorTimeRemapSettings* settings,
    double* source_seconds_out,
    float* blend_out,
    std::size_t output_capacity,
    std::size_t* output_count,
    std::uint64_t* digest) {
  if (keyframes == nullptr || settings == nullptr || output_count == nullptr || digest == nullptr) return 1u;
  std::vector<digitor::TimeRemapKeyframe> keys;
  keys.reserve(keyframe_count);
  for (std::size_t i = 0; i < keyframe_count; ++i) {
    keys.push_back({keyframes[i].timeline_seconds, keyframes[i].source_seconds,
                    keyframes[i].incoming_slope, keyframes[i].outgoing_slope});
  }
  digitor::TimeRemapSettings cpp_settings;
  cpp_settings.interpolation = static_cast<digitor::TimeRemapInterpolation>(settings->interpolation);
  cpp_settings.preserve_audio_pitch = settings->preserve_audio_pitch != 0u;
  cpp_settings.clamp_to_source = settings->clamp_to_source != 0u;
  cpp_settings.source_duration_seconds = settings->source_duration_seconds;
  const auto plan = digitor::build_time_remap_plan(keys, output_duration_seconds, output_fps,
                                                    source_fps, cpp_settings);
  if (plan.samples.empty()) return 2u;
  *output_count = plan.samples.size();
  *digest = plan.digest;
  if (source_seconds_out == nullptr || blend_out == nullptr || output_capacity < plan.samples.size()) return 3u;
  for (std::size_t i = 0; i < plan.samples.size(); ++i) {
    source_seconds_out[i] = plan.samples[i].source_seconds;
    blend_out[i] = plan.samples[i].blend;
  }
  return 0u;
}
