#ifndef DIGITOR_TIMESTAMP_NORMALIZATION_HPP
#define DIGITOR_TIMESTAMP_NORMALIZATION_HPP

#include <cstdint>
#include <vector>

namespace digitor {

struct AudioLatencyCalibration {
  std::int64_t device_output_latency_us{};
  std::int64_t callback_buffer_latency_us{};
  std::int64_t user_offset_us{};
};

[[nodiscard]] std::int64_t compensated_audio_position_us(
    std::int64_t rendered_position_us,
    const AudioLatencyCalibration& calibration);

struct RawVideoTimestamp {
  std::int64_t pts_us{};
  std::int64_t duration_us{};
  std::uint64_t decode_order{};
};

struct NormalizedVideoTimestamp {
  std::int64_t pts_us{};
  std::int64_t duration_us{};
  std::uint64_t decode_order{};
  bool discontinuity_corrected{};
  bool duration_inferred{};
};

struct VfrNormalizationConfig {
  std::int64_t minimum_duration_us{1000};
  std::int64_t default_duration_us{33333};
  std::int64_t discontinuity_threshold_us{500000};
};

[[nodiscard]] std::vector<NormalizedVideoTimestamp> normalize_vfr_timestamps(
    const std::vector<RawVideoTimestamp>& input,
    const VfrNormalizationConfig& config = {});

}  // namespace digitor

#endif
