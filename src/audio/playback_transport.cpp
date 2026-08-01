#include "digitor/playback_transport.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace digitor {
namespace {
std::int64_t clamp_i64(long double value) noexcept {
  const auto lo = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
  const auto hi = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
  if (value <= lo) return std::numeric_limits<std::int64_t>::min();
  if (value >= hi) return std::numeric_limits<std::int64_t>::max();
  return static_cast<std::int64_t>(value);
}
std::int64_t clamp_position(std::int64_t value, std::int64_t duration) noexcept {
  return std::clamp<std::int64_t>(value, 0, std::max<std::int64_t>(duration, 0));
}
}  // namespace

PlaybackTransport::PlaybackTransport(std::int64_t duration_us,
                                     std::int64_t manual_offset_us,
                                     bool manual_override) noexcept
    : sync_(manual_offset_us, manual_override), duration_us_(std::max<std::int64_t>(duration_us, 0)) {}

std::int64_t PlaybackTransport::unclamped_position(std::int64_t now_us) const noexcept {
  if (state_ != PlaybackState::playing) return anchor_position_us_;
  const auto elapsed = std::max<std::int64_t>(now_us - anchor_time_us_, 0);
  return clamp_i64(static_cast<long double>(anchor_position_us_) +
                   static_cast<long double>(elapsed) * rate_ + correction_us_);
}

std::int64_t PlaybackTransport::position_us(std::int64_t now_us) const noexcept {
  return clamp_position(unclamped_position(now_us), duration_us_);
}

void PlaybackTransport::play(std::int64_t now_us) noexcept {
  if (state_ == PlaybackState::playing) return;
  if (state_ == PlaybackState::completed)
    anchor_position_us_ = rate_ < 0.0 ? duration_us_ : 0;
  anchor_time_us_ = now_us;
  state_ = PlaybackState::playing;
}

void PlaybackTransport::pause(std::int64_t now_us) noexcept {
  anchor_position_us_ = position_us(now_us);
  state_ = PlaybackState::paused;
}

void PlaybackTransport::stop() noexcept {
  state_ = PlaybackState::stopped;
  anchor_position_us_ = 0;
  anchor_time_us_ = 0;
  drift_us_ = 0;
  correction_us_ = 0;
}

void PlaybackTransport::seek(std::int64_t position, std::int64_t now_us) noexcept {
  anchor_position_us_ = clamp_position(position, duration_us_);
  anchor_time_us_ = now_us;
  correction_us_ = 0;
  drift_us_ = 0;
  ++seek_generation_;
  const bool at_forward_end = rate_ >= 0.0 && anchor_position_us_ >= duration_us_;
  const bool at_reverse_end = rate_ < 0.0 && anchor_position_us_ <= 0;
  state_ = (at_forward_end || at_reverse_end) ? PlaybackState::completed : PlaybackState::seeking;
}

bool PlaybackTransport::set_rate(double rate, std::int64_t now_us) noexcept {
  const auto magnitude = std::fabs(rate);
  if (!std::isfinite(rate) || magnitude < 0.25 || magnitude > 4.0) return false;
  anchor_position_us_ = position_us(now_us);
  anchor_time_us_ = now_us;
  rate_ = rate;
  correction_us_ = 0;
  return true;
}

void PlaybackTransport::notify_audio_device_changed() noexcept {
  sync_.notify_audio_device_changed();
  ++device_generation_;
}

bool PlaybackTransport::refresh_audio_device(std::int64_t now_us) noexcept {
  return sync_.refresh_probe(now_us);
}

std::size_t PlaybackTransport::select_preview_frame(const std::vector<PlaybackFrameTiming>& frames,
                                                     std::int64_t now_us) const noexcept {
  return sync_.select_frame(frames, position_us(now_us));
}

std::size_t PlaybackTransport::select_export_frame(const std::vector<PlaybackFrameTiming>& frames,
                                                    std::int64_t timeline_us) const noexcept {
  return sync_.select_frame(frames, clamp_position(timeline_us, duration_us_));
}

std::int64_t PlaybackTransport::update_audio_clock(std::int64_t raw_audio_clock_us,
                                                   std::int64_t now_us) noexcept {
  const auto audio = sync_.compensated_clock_us(raw_audio_clock_us);
  const auto transport = position_us(now_us);
  drift_us_ = audio - transport;
  constexpr std::int64_t kDeadbandUs = 2000;
  constexpr std::int64_t kMaxStepUs = 5000;
  if (std::llabs(drift_us_) <= kDeadbandUs) {
    correction_us_ = 0;
  } else {
    correction_us_ = std::clamp<std::int64_t>(drift_us_ / 4, -kMaxStepUs, kMaxStepUs);
  }
  return transport + correction_us_;
}

PlaybackTransportSnapshot PlaybackTransport::snapshot(std::int64_t now_us) const noexcept {
  return {state_, position_us(now_us), duration_us_, rate_, drift_us_, correction_us_,
          seek_generation_, device_generation_};
}

}  // namespace digitor
