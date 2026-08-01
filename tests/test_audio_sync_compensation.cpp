#include "digitor/audio_sync_c_api.h"
#include "digitor/audio_sync_compensation.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

int main() {
  digitor::AudioLatencyProbeResult measured;
  measured.backend = digitor::AudioLatencyBackend::wasapi;
  measured.available = true;
  measured.total_latency_us = 12000;

  const auto automatic = digitor::make_audio_sync_compensation(measured, -2000, false);
  assert(automatic.measured_available);
  assert(automatic.effective_offset_us == 10000);
  assert(digitor::compensate_audio_clock_us(50000, automatic) == 60000);

  const auto overridden = digitor::make_audio_sync_compensation(measured, 3500, true);
  assert(overridden.manual_override);
  assert(overridden.effective_offset_us == 3500);

  digitor::AudioLatencyProbeResult unavailable;
  const auto fallback = digitor::make_audio_sync_compensation(unavailable, -4500, false);
  assert(!fallback.measured_available);
  assert(fallback.effective_offset_us == -4500);

  const auto saturated = digitor::compensate_audio_clock_us(
      std::numeric_limits<std::int64_t>::max() - 2,
      digitor::AudioSyncCompensation{digitor::AudioLatencyBackend::unavailable,
                                     false, true, 0, 10, 10});
  assert(saturated == std::numeric_limits<std::int64_t>::max());

  const auto c_snapshot = digitor_audio_sync_probe(1234, 1);
  assert(c_snapshot.manual_override == 1);
  assert(c_snapshot.effective_offset_us == 1234);
  assert(digitor_audio_sync_compensate_clock(5000, c_snapshot) == 6234);
  return 0;
}
