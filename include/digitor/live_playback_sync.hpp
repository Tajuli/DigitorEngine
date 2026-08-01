#pragma once

#include "digitor/audio_sync_compensation.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace digitor {

struct PlaybackFrameTiming {
  std::int64_t pts_us{};
  std::int64_t duration_us{};
};

struct PlaybackSyncSnapshot {
  AudioSyncCompensation compensation{};
  std::uint64_t probe_generation{};
  std::int64_t last_probe_time_us{};
  bool device_change_pending{};
};

class LivePlaybackSync final {
 public:
  explicit LivePlaybackSync(std::int64_t manual_offset_us = 0,
                            bool manual_override = false) noexcept;

  void set_manual_offset(std::int64_t offset_us, bool override_mode) noexcept;
  void notify_audio_device_changed() noexcept;
  [[nodiscard]] bool refresh_probe(std::int64_t now_us) noexcept;
  [[nodiscard]] std::int64_t compensated_clock_us(std::int64_t raw_audio_clock_us) const noexcept;
  [[nodiscard]] std::size_t select_frame(const std::vector<PlaybackFrameTiming>& frames,
                                         std::int64_t raw_audio_clock_us) const noexcept;
  [[nodiscard]] PlaybackSyncSnapshot snapshot() const noexcept;

 private:
  std::int64_t manual_offset_us_{};
  bool manual_override_{};
  bool device_change_pending_{true};
  std::uint64_t probe_generation_{};
  std::int64_t last_probe_time_us_{};
  AudioSyncCompensation compensation_{};
};

}  // namespace digitor
