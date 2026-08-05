#include "digitor/production_keyframe_automation.hpp"

#include <cmath>
#include <cstdint>

namespace {

bool near(double a, double b, double tolerance = 1.0e-9) {
  return std::fabs(a - b) <= tolerance;
}

}  // namespace

int main() {
  using namespace digitor;

  KeyframeTrack linear;
  linear.keyframes = {
      {0.0, 0.0, KeyframeInterpolation::linear},
      {1.0, 10.0, KeyframeInterpolation::linear},
  };
  const auto preview = evaluate_keyframe_track(linear, 0.5);
  const auto export_value = evaluate_keyframe_track(linear, 0.5);
  if (preview.status != KeyframeStatus::ready ||
      export_value.status != KeyframeStatus::ready ||
      !near(preview.value, 5.0) || preview.digest != export_value.digest) {
    return 1;
  }

  KeyframeTrack hold = linear;
  hold.keyframes[0].interpolation = KeyframeInterpolation::hold;
  if (!near(evaluate_keyframe_track(hold, 0.75).value, 0.0)) {
    return 2;
  }

  KeyframeTrack eased = linear;
  eased.keyframes[0].interpolation = KeyframeInterpolation::ease_in_out;
  if (!near(evaluate_keyframe_track(eased, 0.5).value, 5.0)) {
    return 3;
  }

  KeyframeTrack bezier = linear;
  bezier.keyframes[0].interpolation = KeyframeInterpolation::cubic_bezier;
  bezier.keyframes[0].control_out_x = 0.42;
  bezier.keyframes[0].control_out_y = 0.0;
  bezier.keyframes[0].control_in_x = 0.58;
  bezier.keyframes[0].control_in_y = 1.0;
  const auto bezier_mid = evaluate_keyframe_track(bezier, 0.5);
  if (bezier_mid.status != KeyframeStatus::ready ||
      !near(bezier_mid.value, 5.0, 1.0e-6)) {
    return 4;
  }

  if (!near(evaluate_keyframe_track(linear, -1.0).value, 0.0) ||
      !near(evaluate_keyframe_track(linear, 2.0).value, 10.0)) {
    return 5;
  }

  KeyframeTrack unclamped = linear;
  unclamped.clamp_before_first = false;
  unclamped.clamp_after_last = false;
  if (evaluate_keyframe_track(unclamped, -0.1).status !=
          KeyframeStatus::invalid ||
      evaluate_keyframe_track(unclamped, 1.1).status !=
          KeyframeStatus::invalid) {
    return 6;
  }

  KeyframeTrack invalid = linear;
  invalid.keyframes[1].time_seconds = 0.0;
  if (validate_keyframe_track(invalid) != KeyframeStatus::invalid) {
    return 7;
  }

  DigitorKeyframe c_keyframes[2]{};
  c_keyframes[0].time_seconds = 0.0;
  c_keyframes[0].value = 2.0;
  c_keyframes[0].interpolation =
      static_cast<std::uint32_t>(KeyframeInterpolation::linear);
  c_keyframes[0].control_out_x = 1.0 / 3.0;
  c_keyframes[0].control_out_y = 1.0 / 3.0;
  c_keyframes[0].control_in_x = 2.0 / 3.0;
  c_keyframes[0].control_in_y = 2.0 / 3.0;
  c_keyframes[1] = c_keyframes[0];
  c_keyframes[1].time_seconds = 2.0;
  c_keyframes[1].value = 6.0;
  DigitorKeyframeEvaluation c_output{};
  if (digitor_evaluate_keyframes(c_keyframes, 2u, 1.0, 1u, 1u,
                                 &c_output) != 0u ||
      !near(c_output.value, 4.0) || c_output.digest == 0u) {
    return 8;
  }

  return 0;
}
