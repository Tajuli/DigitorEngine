#include "digitor/audio_sync_compensation.hpp"

#include <limits>

namespace digitor {
namespace {

std::int64_t saturating_add(std::int64_t a, std::int64_t b) noexcept {
  if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) {
    return std::numeric_limits<std::int64_t>::max();
  }
  if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b) {
    return std::numeric_limits<std::int64_t>::min();
  }
  return a + b;
}

}  // namespace

AudioSyncCompensation make_audio_sync_compensation(
    const AudioLatencyProbeResult& probe,
    std::int64_t manual_offset_us,
    bool manual_override) noexcept {
  AudioSyncCompensation result;
  result.backend = probe.backend;
  result.measured_available = probe.available && probe.total_latency_us >= 0;
  result.manual_override = manual_override;
  result.measured_latency_us = result.measured_available ? probe.total_latency_us : 0;
  result.manual_offset_us = manual_offset_us;
  result.effective_offset_us = manual_override
      ? manual_offset_us
      : saturating_add(result.measured_latency_us, manual_offset_us);
  return result;
}

std::int64_t compensate_audio_clock_us(
    std::int64_t audio_clock_us,
    const AudioSyncCompensation& compensation) noexcept {
  return saturating_add(audio_clock_us, compensation.effective_offset_us);
}

}  // namespace digitor
