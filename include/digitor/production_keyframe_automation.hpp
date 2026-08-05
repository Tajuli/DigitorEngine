#pragma once

#include <cstdint>
#include <vector>

namespace digitor {

enum class KeyframeInterpolation : std::uint32_t {
  hold,
  linear,
  ease_in,
  ease_out,
  ease_in_out,
  cubic_bezier
};

enum class KeyframeStatus : std::uint32_t { invalid, ready };

struct Keyframe final {
  double time_seconds{};
  double value{};
  KeyframeInterpolation interpolation{KeyframeInterpolation::linear};
  double control_out_x{0.333333333333};
  double control_out_y{0.333333333333};
  double control_in_x{0.666666666667};
  double control_in_y{0.666666666667};
};

struct KeyframeTrack final {
  std::vector<Keyframe> keyframes;
  bool clamp_before_first{true};
  bool clamp_after_last{true};
};

struct KeyframeEvaluation final {
  KeyframeStatus status{KeyframeStatus::invalid};
  double value{};
  std::uint32_t left_index{};
  std::uint32_t right_index{};
  std::uint64_t digest{};
};

KeyframeStatus validate_keyframe_track(const KeyframeTrack&) noexcept;
KeyframeEvaluation evaluate_keyframe_track(const KeyframeTrack&,
                                           double time_seconds) noexcept;
std::uint64_t keyframe_evaluation_digest(const KeyframeEvaluation&) noexcept;

}  // namespace digitor

extern "C" {

struct DigitorKeyframe {
  double time_seconds;
  double value;
  std::uint32_t interpolation;
  double control_out_x;
  double control_out_y;
  double control_in_x;
  double control_in_y;
};

struct DigitorKeyframeEvaluation {
  double value;
  std::uint32_t left_index;
  std::uint32_t right_index;
  std::uint64_t digest;
};

std::uint32_t digitor_evaluate_keyframes(const DigitorKeyframe* keyframes,
                                         std::uint32_t keyframe_count,
                                         double time_seconds,
                                         std::uint32_t clamp_before_first,
                                         std::uint32_t clamp_after_last,
                                         DigitorKeyframeEvaluation* output);

}
