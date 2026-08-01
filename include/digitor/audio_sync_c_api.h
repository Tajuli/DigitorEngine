#ifndef DIGITOR_AUDIO_SYNC_C_API_H
#define DIGITOR_AUDIO_SYNC_C_API_H

#include <stdint.h>

#if defined(_WIN32) && !defined(DIGITOR_ENGINE_STATIC)
#  if defined(DIGITOR_ENGINE_BUILD)
#    define DIGITOR_AUDIO_SYNC_API __declspec(dllexport)
#  else
#    define DIGITOR_AUDIO_SYNC_API __declspec(dllimport)
#  endif
#else
#  define DIGITOR_AUDIO_SYNC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DigitorAudioSyncSnapshot {
  int32_t backend;
  int32_t measured_available;
  int32_t manual_override;
  int64_t measured_latency_us;
  int64_t manual_offset_us;
  int64_t effective_offset_us;
} DigitorAudioSyncSnapshot;

DIGITOR_AUDIO_SYNC_API DigitorAudioSyncSnapshot digitor_audio_sync_probe(
    int64_t manual_offset_us,
    int32_t manual_override);

DIGITOR_AUDIO_SYNC_API int64_t digitor_audio_sync_compensate_clock(
    int64_t audio_clock_us,
    DigitorAudioSyncSnapshot snapshot);

#ifdef __cplusplus
}
#endif

#endif
