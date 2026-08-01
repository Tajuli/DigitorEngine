#pragma once

#include "digitor/live_playback_sync.hpp"

#include <cstdint>
#include <vector>

namespace digitor {

enum class PlaybackState { stopped, playing, paused, seeking, completed };

struct PlaybackTransportSnapshot {
  PlaybackState state{PlaybackState::stopped};
  std::int64_t position_us{};
  std::int64_t duration_us{};
  double rate{1.0};
  std::int64_t drift_us{};
  std::int64_t correction_us{};
  std::uint64_t seek_generation{};
  std::uint64_t device_generation{};
};

class PlaybackTransport final {
 public:
  explicit PlaybackTransport(std::int64_t duration_us = 0,
                             std::int64_t manual_offset_us = 0,
                             bool manual_override = false) noexcept;
  void play(std::int64_t monotonic_now_us) noexcept;
  void pause(std::int64_t monotonic_now_us) noexcept;
  void stop() noexcept;
  void seek(std::int64_t position_us, std::int64_t monotonic_now_us) noexcept;
  [[nodiscard]] bool set_rate(double rate, std::int64_t monotonic_now_us) noexcept;
  void notify_audio_device_changed() noexcept;
  [[nodiscard]] bool refresh_audio_device(std::int64_t monotonic_now_us) noexcept;
  [[nodiscard]] std::int64_t position_us(std::int64_t monotonic_now_us) const noexcept;
  [[nodiscard]] std::size_t select_preview_frame(const std::vector<PlaybackFrameTiming>& frames,
                                                  std::int64_t monotonic_now_us) const noexcept;
  [[nodiscard]] std::size_t select_export_frame(const std::vector<PlaybackFrameTiming>& frames,
                                                 std::int64_t timeline_us) const noexcept;
  [[nodiscard]] std::int64_t update_audio_clock(std::int64_t raw_audio_clock_us,
                                                std::int64_t monotonic_now_us) noexcept;
  [[nodiscard]] PlaybackTransportSnapshot snapshot(std::int64_t monotonic_now_us) const noexcept;

 private:
  [[nodiscard]] std::int64_t unclamped_position(std::int64_t monotonic_now_us) const noexcept;
  LivePlaybackSync sync_;
  PlaybackState state_{PlaybackState::stopped};
  std::int64_t duration_us_{};
  std::int64_t anchor_position_us_{};
  std::int64_t anchor_time_us_{};
  double rate_{1.0};
  std::int64_t drift_us_{};
  std::int64_t correction_us_{};
  std::uint64_t seek_generation_{};
  std::uint64_t device_generation_{};
};

}  // namespace digitor
