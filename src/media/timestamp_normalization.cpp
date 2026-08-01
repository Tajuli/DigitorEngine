#include "digitor/timestamp_normalization.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace digitor {
namespace {
std::int64_t checked_add(std::int64_t a, std::int64_t b) {
  if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
      (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) {
    throw std::overflow_error("timestamp overflow");
  }
  return a + b;
}
}

std::int64_t compensated_audio_position_us(
    std::int64_t rendered_position_us,
    const AudioLatencyCalibration& calibration) {
  if (calibration.device_output_latency_us < 0 ||
      calibration.callback_buffer_latency_us < 0) {
    throw std::invalid_argument("audio latency must be non-negative");
  }
  auto result = checked_add(rendered_position_us, -calibration.device_output_latency_us);
  result = checked_add(result, -calibration.callback_buffer_latency_us);
  return checked_add(result, calibration.user_offset_us);
}

std::vector<NormalizedVideoTimestamp> normalize_vfr_timestamps(
    const std::vector<RawVideoTimestamp>& input,
    const VfrNormalizationConfig& config) {
  if (config.minimum_duration_us <= 0 || config.default_duration_us <= 0 ||
      config.discontinuity_threshold_us < config.minimum_duration_us) {
    throw std::invalid_argument("invalid VFR normalization configuration");
  }
  std::vector<RawVideoTimestamp> ordered = input;
  std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
    return a.decode_order < b.decode_order;
  });
  std::vector<NormalizedVideoTimestamp> output;
  output.reserve(ordered.size());
  std::int64_t next_pts = 0;
  bool first = true;
  for (std::size_t i = 0; i < ordered.size(); ++i) {
    const auto& frame = ordered[i];
    auto duration = frame.duration_us;
    bool inferred = false;
    if (duration < config.minimum_duration_us) {
      if (i + 1 < ordered.size() && ordered[i + 1].pts_us > frame.pts_us) {
        const auto candidate = ordered[i + 1].pts_us - frame.pts_us;
        if (candidate >= config.minimum_duration_us &&
            candidate <= config.discontinuity_threshold_us) {
          duration = candidate;
        }
      }
      if (duration < config.minimum_duration_us) duration = config.default_duration_us;
      inferred = true;
    }
    bool corrected = false;
    auto pts = frame.pts_us;
    if (first) {
      next_pts = pts;
      first = false;
    } else if (pts < next_pts || pts - next_pts > config.discontinuity_threshold_us) {
      pts = next_pts;
      corrected = true;
    }
    output.push_back({pts, duration, frame.decode_order, corrected, inferred});
    next_pts = checked_add(pts, duration);
  }
  return output;
}

}  // namespace digitor
