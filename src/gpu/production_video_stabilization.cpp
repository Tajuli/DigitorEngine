#include "digitor/production_video_stabilization.hpp"

#include <algorithm>
#include <cmath>

namespace digitor {
namespace {
float clamp01(float v) noexcept { return std::clamp(v, 0.0f, 1.0f); }
bool valid_settings(const StabilizationSettings& s) noexcept {
  return s.strength >= 0.0f && s.strength <= 1.0f && s.smoothing >= 0.0f && s.smoothing < 1.0f &&
         s.max_zoom >= 1.0f && s.max_zoom <= 2.0f;
}
std::uint64_t append(std::uint64_t h, const void* p, std::size_t n) noexcept {
  const auto* b = static_cast<const unsigned char*>(p);
  for (std::size_t i = 0; i < n; ++i) { h ^= static_cast<std::uint64_t>(b[i]); h *= 1099511628211ull; }
  return h;
}
}  // namespace

std::uint64_t stabilization_plan_digest(const StabilizationPlan& plan) noexcept {
  std::uint64_t h = 1469598103934665603ull;
  if (!plan.transforms.empty()) {
    h = append(h, plan.transforms.data(), plan.transforms.size() * sizeof(StabilizationTransform));
  }
  return h;
}

StabilizationPlan build_stabilization_plan(const std::vector<MotionSample>& samples,
                                           const StabilizationSettings& settings) {
  StabilizationPlan plan;
  if (samples.empty() || !valid_settings(settings)) return plan;
  plan.transforms.reserve(samples.size());
  float smooth_x{}, smooth_y{}, smooth_r{};
  for (const auto& sample : samples) {
    const float confidence = clamp01(sample.confidence);
    const float alpha = std::clamp(1.0f - settings.smoothing, 0.001f, 1.0f);
    smooth_x += alpha * (sample.dx - smooth_x);
    smooth_y += alpha * (sample.dy - smooth_y);
    smooth_r += alpha * (sample.rotation - smooth_r);
    StabilizationTransform transform;
    transform.translate_x = -(sample.dx - smooth_x) * settings.strength * confidence;
    transform.translate_y = -(sample.dy - smooth_y) * settings.strength * confidence;
    transform.rotation = settings.lock_horizon ? -sample.rotation * settings.strength * confidence
                                                : -(sample.rotation - smooth_r) * settings.strength * confidence;
    const float motion = std::sqrt(transform.translate_x * transform.translate_x +
                                   transform.translate_y * transform.translate_y) +
                         std::abs(transform.rotation) * 0.5f;
    transform.zoom = std::clamp(1.0f + motion * 0.01f, 1.0f, settings.max_zoom);
    if (settings.rolling_shutter_correction) transform.zoom = std::min(settings.max_zoom, transform.zoom + 0.005f);
    plan.transforms.push_back(transform);
  }
  plan.digest = stabilization_plan_digest(plan);
  return plan;
}

StabilizationResult dispatch_stabilized_frame(const StabilizationDispatchPacket& packet,
                                              const StabilizationDispatch& dispatch) {
  StabilizationResult result;
  if (packet.backend == StabilizationBackend::cpu || packet.input_handle == 0u ||
      packet.output_handle == 0u || packet.command_handle == 0u || packet.width == 0u ||
      packet.height == 0u || packet.transform.zoom < 1.0f) return result;
  if (!dispatch) { result.status = StabilizationStatus::backend_unavailable; return result; }
  result.status = dispatch(packet) ? StabilizationStatus::ready : StabilizationStatus::dispatch_failed;
  std::uint64_t h = 1469598103934665603ull;
  h = append(h, &packet.backend, sizeof(packet.backend));
  h = append(h, &packet.width, sizeof(packet.width));
  h = append(h, &packet.height, sizeof(packet.height));
  h = append(h, &packet.transform, sizeof(packet.transform));
  result.digest = h;
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_build_stabilization_plan(
    const DigitorMotionSample* samples, std::size_t sample_count,
    const DigitorStabilizationSettings* settings, DigitorStabilizationTransform* transforms,
    std::uint64_t* digest) {
  if (!samples || sample_count == 0u || !settings || !transforms || !digest) return 1u;
  std::vector<digitor::MotionSample> native;
  native.reserve(sample_count);
  for (std::size_t i = 0; i < sample_count; ++i) {
    native.push_back({samples[i].timestamp, samples[i].dx, samples[i].dy, samples[i].rotation,
                      samples[i].confidence});
  }
  digitor::StabilizationSettings s;
  s.strength = settings->strength; s.smoothing = settings->smoothing; s.max_zoom = settings->max_zoom;
  s.lock_horizon = settings->lock_horizon != 0u;
  s.rolling_shutter_correction = settings->rolling_shutter_correction != 0u;
  const auto plan = digitor::build_stabilization_plan(native, s);
  if (plan.transforms.size() != sample_count) return 2u;
  for (std::size_t i = 0; i < sample_count; ++i) {
    transforms[i] = {plan.transforms[i].translate_x, plan.transforms[i].translate_y,
                     plan.transforms[i].rotation, plan.transforms[i].zoom};
  }
  *digest = plan.digest;
  return 0u;
}
