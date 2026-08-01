#pragma once

#include "digitor/audio_latency_probe.hpp"

#include <cstdint>

namespace digitor {

struct AudioSyncCompensation {
  AudioLatencyBackend backend{AudioLatencyBackend::unavailable};
  bool measured_available{};
  bool manual_override{};
  std::int64_t measured_latency_us{};
  std::int64_t manual_offset_us{};
  std::int64_t effective_offset_us{};
};

[[nodiscard]] AudioSyncCompensation make_audio_sync_compensation(
    const AudioLatencyProbeResult& probe,
    std::int64_t manual_offset_us = 0,
    bool manual_override = false) noexcept;

[[nodiscard]] std::int64_t compensate_audio_clock_us(
    std::int64_t audio_clock_us,
    const AudioSyncCompensation& compensation) noexcept;

}  // namespace digitor
