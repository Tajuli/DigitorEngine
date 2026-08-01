#include "digitor/av_sync.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace digitor {

AvSyncClock::AvSyncClock(AvSyncConfig config) : config_(config) {
  if (config_.present_tolerance_us < 0 || config_.drop_threshold_us < 0 ||
      config_.drop_threshold_us < config_.present_tolerance_us) {
    throw std::invalid_argument("invalid A/V sync thresholds");
  }
}

void AvSyncClock::reset(std::int64_t position_us,
                        std::uint64_t seek_epoch) noexcept {
  position_us_ = std::max<std::int64_t>(0, position_us);
  seek_epoch_ = seek_epoch;
  fractional_numerator_ = 0;
}

void AvSyncClock::advance_audio(std::uint64_t rendered_frames,
                                std::uint32_t sample_rate) {
  if (sample_rate == 0) {
    throw std::invalid_argument("sample rate must be non-zero");
  }
  if (rendered_frames >
      (std::numeric_limits<std::uint64_t>::max() - fractional_numerator_) /
          1000000ULL) {
    throw std::overflow_error("audio clock overflow");
  }
  const auto numerator =
      fractional_numerator_ + rendered_frames * 1000000ULL;
  const auto whole_us = numerator / sample_rate;
  fractional_numerator_ = numerator % sample_rate;
  if (whole_us > static_cast<std::uint64_t>(
                     std::numeric_limits<std::int64_t>::max() - position_us_)) {
    throw std::overflow_error("timeline position overflow");
  }
  position_us_ += static_cast<std::int64_t>(whole_us);
}

std::int64_t AvSyncClock::position_us() const noexcept { return position_us_; }

std::uint64_t AvSyncClock::seek_epoch() const noexcept { return seek_epoch_; }

AvSyncResult AvSyncClock::select_video_frame(
    const std::vector<TimestampedVideoFrame>& queue) const {
  const TimestampedVideoFrame* best = nullptr;
  for (const auto& frame : queue) {
    if (frame.seek_epoch != seek_epoch_) {
      continue;
    }
    if (!best || frame.pts_us <= position_us_) {
      if (!best || frame.pts_us > best->pts_us) {
        best = &frame;
      }
    }
  }

  if (!best) {
    const bool has_stale = std::any_of(queue.begin(), queue.end(), [&](const auto& frame) {
      return frame.seek_epoch != seek_epoch_;
    });
    return {has_stale ? AvSyncDecision::stale : AvSyncDecision::hold, 0,
            std::nullopt};
  }

  const auto drift = position_us_ - best->pts_us;
  if (drift > config_.drop_threshold_us) {
    return {AvSyncDecision::drop, drift, *best};
  }
  if (drift <= config_.present_tolerance_us) {
    return {AvSyncDecision::present, drift, *best};
  }
  return {AvSyncDecision::hold, drift, *best};
}

std::uint64_t av_parity_hash(
    const std::vector<TimestampedVideoFrame>& frames,
    std::uint64_t seek_epoch) noexcept {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const auto& frame : frames) {
    if (frame.seek_epoch != seek_epoch) {
      continue;
    }
    hash ^= static_cast<std::uint64_t>(frame.pts_us);
    hash *= 1099511628211ULL;
    hash ^= frame.content_hash;
    hash *= 1099511628211ULL;
  }
  return hash;
}

}  // namespace digitor
