#ifndef DIGITOR_AV_SYNC_HPP
#define DIGITOR_AV_SYNC_HPP

#include <cstdint>
#include <optional>
#include <vector>

namespace digitor {

enum class AvSyncDecision { present, hold, drop, stale };

struct TimestampedVideoFrame {
  std::int64_t pts_us{};
  std::uint64_t seek_epoch{};
  std::uint64_t content_hash{};
};

struct AvSyncConfig {
  std::int64_t present_tolerance_us{20000};
  std::int64_t drop_threshold_us{50000};
};

struct AvSyncResult {
  AvSyncDecision decision{AvSyncDecision::hold};
  std::int64_t drift_us{};
  std::optional<TimestampedVideoFrame> frame;
};

class AvSyncClock {
 public:
  explicit AvSyncClock(AvSyncConfig config = {});

  void reset(std::int64_t position_us, std::uint64_t seek_epoch) noexcept;
  void advance_audio(std::uint64_t rendered_frames, std::uint32_t sample_rate);
  [[nodiscard]] std::int64_t position_us() const noexcept;
  [[nodiscard]] std::uint64_t seek_epoch() const noexcept;
  [[nodiscard]] AvSyncResult select_video_frame(
      const std::vector<TimestampedVideoFrame>& queue) const;

 private:
  AvSyncConfig config_;
  std::int64_t position_us_{};
  std::uint64_t seek_epoch_{};
  std::uint64_t fractional_numerator_{};
};

[[nodiscard]] std::uint64_t av_parity_hash(
    const std::vector<TimestampedVideoFrame>& frames,
    std::uint64_t seek_epoch) noexcept;

}  // namespace digitor

#endif
