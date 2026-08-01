#include "digitor/audio_sync_c_api.h"

#include "digitor/audio_latency_probe.hpp"
#include "digitor/audio_sync_compensation.hpp"

extern "C" {

DigitorAudioSyncSnapshot digitor_audio_sync_probe(
    int64_t manual_offset_us,
    int32_t manual_override) {
  DigitorAudioSyncSnapshot snapshot{};
  try {
    const auto compensation = digitor::make_audio_sync_compensation(
        digitor::probe_default_audio_output(),
        manual_offset_us,
        manual_override != 0);
    snapshot.backend = static_cast<int32_t>(compensation.backend);
    snapshot.measured_available = compensation.measured_available ? 1 : 0;
    snapshot.manual_override = compensation.manual_override ? 1 : 0;
    snapshot.measured_latency_us = compensation.measured_latency_us;
    snapshot.manual_offset_us = compensation.manual_offset_us;
    snapshot.effective_offset_us = compensation.effective_offset_us;
  } catch (...) {
  }
  return snapshot;
}

int64_t digitor_audio_sync_compensate_clock(
    int64_t audio_clock_us,
    DigitorAudioSyncSnapshot snapshot) {
  try {
    digitor::AudioSyncCompensation compensation;
    compensation.backend = static_cast<digitor::AudioLatencyBackend>(snapshot.backend);
    compensation.measured_available = snapshot.measured_available != 0;
    compensation.manual_override = snapshot.manual_override != 0;
    compensation.measured_latency_us = snapshot.measured_latency_us;
    compensation.manual_offset_us = snapshot.manual_offset_us;
    compensation.effective_offset_us = snapshot.effective_offset_us;
    return digitor::compensate_audio_clock_us(audio_clock_us, compensation);
  } catch (...) {
    return audio_clock_us;
  }
}

}  // extern "C"
