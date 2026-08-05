#include "digitor/production_keyframe_automation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace digitor {
namespace {

[[nodiscard]] bool finite(double value) noexcept {
  return std::isfinite(value);
}

[[nodiscard]] bool valid_interpolation(KeyframeInterpolation value) noexcept {
  return static_cast<std::uint32_t>(value) <=
         static_cast<std::uint32_t>(KeyframeInterpolation::cubic_bezier);
}

[[nodiscard]] double smoothstep(double t) noexcept {
  return t * t * (3.0 - 2.0 * t);
}

[[nodiscard]] double cubic(double p0, double p1, double p2, double p3,
                           double t) noexcept {
  const double u = 1.0 - t;
  return u * u * u * p0 + 3.0 * u * u * t * p1 +
         3.0 * u * t * t * p2 + t * t * t * p3;
}

[[nodiscard]] double cubic_derivative(double p0, double p1, double p2,
                                      double p3, double t) noexcept {
  const double u = 1.0 - t;
  return 3.0 * u * u * (p1 - p0) + 6.0 * u * t * (p2 - p1) +
         3.0 * t * t * (p3 - p2);
}

[[nodiscard]] double solve_bezier_x(double target, double x1,
                                    double x2) noexcept {
  double t = target;
  for (int iteration = 0; iteration < 8; ++iteration) {
    const double value = cubic(0.0, x1, x2, 1.0, t) - target;
    const double slope = cubic_derivative(0.0, x1, x2, 1.0, t);
    if (std::fabs(slope) < 1.0e-10) {
      break;
    }
    t = std::clamp(t - value / slope, 0.0, 1.0);
  }
  return t;
}

[[nodiscard]] double interpolation_progress(const Keyframe& left,
                                             double t) noexcept {
  switch (left.interpolation) {
    case KeyframeInterpolation::hold:
      return 0.0;
    case KeyframeInterpolation::linear:
      return t;
    case KeyframeInterpolation::ease_in:
      return t * t;
    case KeyframeInterpolation::ease_out:
      return 1.0 - (1.0 - t) * (1.0 - t);
    case KeyframeInterpolation::ease_in_out:
      return smoothstep(t);
    case KeyframeInterpolation::cubic_bezier: {
      const double solved = solve_bezier_x(t, left.control_out_x,
                                          left.control_in_x);
      return cubic(0.0, left.control_out_y, left.control_in_y, 1.0, solved);
    }
  }
  return t;
}

std::uint64_t append_digest(std::uint64_t hash, const void* data,
                            std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash;
}

}  // namespace

KeyframeStatus validate_keyframe_track(const KeyframeTrack& track) noexcept {
  if (track.keyframes.empty()) {
    return KeyframeStatus::invalid;
  }
  double previous_time = -1.0;
  for (std::size_t index = 0; index < track.keyframes.size(); ++index) {
    const auto& keyframe = track.keyframes[index];
    if (!finite(keyframe.time_seconds) || !finite(keyframe.value) ||
        !valid_interpolation(keyframe.interpolation) ||
        keyframe.time_seconds < 0.0 ||
        (index > 0u && keyframe.time_seconds <= previous_time) ||
        !finite(keyframe.control_out_x) || !finite(keyframe.control_out_y) ||
        !finite(keyframe.control_in_x) || !finite(keyframe.control_in_y) ||
        keyframe.control_out_x < 0.0 || keyframe.control_out_x > 1.0 ||
        keyframe.control_in_x < 0.0 || keyframe.control_in_x > 1.0) {
      return KeyframeStatus::invalid;
    }
    previous_time = keyframe.time_seconds;
  }
  return KeyframeStatus::ready;
}

std::uint64_t keyframe_evaluation_digest(
    const KeyframeEvaluation& evaluation) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append_digest(hash, &evaluation.value, sizeof(evaluation.value));
  hash = append_digest(hash, &evaluation.left_index,
                       sizeof(evaluation.left_index));
  hash = append_digest(hash, &evaluation.right_index,
                       sizeof(evaluation.right_index));
  return hash;
}

KeyframeEvaluation evaluate_keyframe_track(const KeyframeTrack& track,
                                           double time_seconds) noexcept {
  KeyframeEvaluation result;
  if (validate_keyframe_track(track) != KeyframeStatus::ready ||
      !finite(time_seconds)) {
    return result;
  }

  const auto& first = track.keyframes.front();
  const auto& last = track.keyframes.back();
  if (time_seconds <= first.time_seconds) {
    if (!track.clamp_before_first && time_seconds < first.time_seconds) {
      return result;
    }
    result.status = KeyframeStatus::ready;
    result.value = first.value;
    result.left_index = 0u;
    result.right_index = 0u;
    result.digest = keyframe_evaluation_digest(result);
    return result;
  }
  if (time_seconds >= last.time_seconds) {
    if (!track.clamp_after_last && time_seconds > last.time_seconds) {
      return result;
    }
    const auto index = static_cast<std::uint32_t>(track.keyframes.size() - 1u);
    result.status = KeyframeStatus::ready;
    result.value = last.value;
    result.left_index = index;
    result.right_index = index;
    result.digest = keyframe_evaluation_digest(result);
    return result;
  }

  const auto right = std::upper_bound(
      track.keyframes.begin(), track.keyframes.end(), time_seconds,
      [](double value, const Keyframe& keyframe) {
        return value < keyframe.time_seconds;
      });
  const auto left = right - 1;
  const auto left_index = static_cast<std::uint32_t>(
      std::distance(track.keyframes.begin(), left));
  const auto right_index = left_index + 1u;
  const double duration = right->time_seconds - left->time_seconds;
  const double raw = (time_seconds - left->time_seconds) / duration;
  const double progress = std::clamp(interpolation_progress(*left, raw), 0.0,
                                     1.0);

  result.status = KeyframeStatus::ready;
  result.value = left->value + (right->value - left->value) * progress;
  result.left_index = left_index;
  result.right_index = right_index;
  result.digest = keyframe_evaluation_digest(result);
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_evaluate_keyframes(
    const DigitorKeyframe* keyframes, std::uint32_t keyframe_count,
    double time_seconds, std::uint32_t clamp_before_first,
    std::uint32_t clamp_after_last, DigitorKeyframeEvaluation* output) {
  if (!keyframes || keyframe_count == 0u || !output) {
    return 1u;
  }

  digitor::KeyframeTrack track;
  track.clamp_before_first = clamp_before_first != 0u;
  track.clamp_after_last = clamp_after_last != 0u;
  track.keyframes.reserve(keyframe_count);
  for (std::uint32_t index = 0; index < keyframe_count; ++index) {
    digitor::Keyframe keyframe;
    keyframe.time_seconds = keyframes[index].time_seconds;
    keyframe.value = keyframes[index].value;
    keyframe.interpolation = static_cast<digitor::KeyframeInterpolation>(
        keyframes[index].interpolation);
    keyframe.control_out_x = keyframes[index].control_out_x;
    keyframe.control_out_y = keyframes[index].control_out_y;
    keyframe.control_in_x = keyframes[index].control_in_x;
    keyframe.control_in_y = keyframes[index].control_in_y;
    track.keyframes.push_back(keyframe);
  }

  const auto result = digitor::evaluate_keyframe_track(track, time_seconds);
  if (result.status != digitor::KeyframeStatus::ready) {
    return 2u;
  }
  output->value = result.value;
  output->left_index = result.left_index;
  output->right_index = result.right_index;
  output->digest = result.digest;
  return 0u;
}
