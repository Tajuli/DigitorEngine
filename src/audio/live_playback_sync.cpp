#include "digitor/live_playback_sync.hpp"
#include "digitor/audio_latency_probe.hpp"

#include <algorithm>

namespace digitor {

LivePlaybackSync::LivePlaybackSync(std::int64_t manual_offset_us,
                                   bool manual_override) noexcept
    : manual_offset_us_(manual_offset_us), manual_override_(manual_override) {
  compensation_ =
      make_audio_sync_compensation({}, manual_offset_us_, manual_override_);
}

void LivePlaybackSync::set_manual_offset(std::int64_t offset_us,
                                         bool override_mode) noexcept {
  manual_offset_us_ = offset_us;
  manual_override_ = override_mode;
  const auto probe = probe_default_audio_output();
  compensation_ = make_audio_sync_compensation(
      probe, manual_offset_us_, manual_override_);
}

void LivePlaybackSync::notify_audio_device_changed() noexcept {
  device_change_pending_ = true;
}

bool LivePlaybackSync::refresh_probe(std::int64_t now_us) noexcept {
  const auto probe = probe_default_audio_output();
  compensation_ = make_audio_sync_compensation(
      probe, manual_offset_us_, manual_override_);
  last_probe_time_us_ = now_us;
  device_change_pending_ = false;
  ++probe_generation_;
  return probe.available;
}

std::int64_t LivePlaybackSync::compensated_clock_us(
    std::int64_t raw_audio_clock_us) const noexcept {
  return compensate_audio_clock_us(raw_audio_clock_us, compensation_);
}

std::size_t LivePlaybackSync::select_frame(
    const std::vector<PlaybackFrameTiming>& frames,
    std::int64_t raw_audio_clock_us) const noexcept {
  if (frames.empty()) return 0;

  const auto clock = compensated_clock_us(raw_audio_clock_us);
  for (std::size_t i = 0; i < frames.size(); ++i) {
    const auto duration = std::max<std::int64_t>(frames[i].duration_us, 1);
    if (clock < frames[i].pts_us + duration) {
      return i;
    }
  }
  return frames.size() - 1;
}

PlaybackSyncSnapshot LivePlaybackSync::snapshot() const noexcept {
  return {compensation_, probe_generation_, last_probe_time_us_,
          device_change_pending_};
}

}  // namespace digitor
