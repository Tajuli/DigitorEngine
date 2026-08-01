#ifndef DIGITOR_LIVE_PLAYBACK_SYNC_C_API_H
#define DIGITOR_LIVE_PLAYBACK_SYNC_C_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DigitorLivePlaybackSync DigitorLivePlaybackSync;
typedef struct DigitorPlaybackSyncSnapshot { int64_t effective_offset_us; uint64_t probe_generation; int64_t last_probe_time_us; int32_t measured_available; int32_t device_change_pending; } DigitorPlaybackSyncSnapshot;
DigitorLivePlaybackSync* digitor_live_playback_sync_create(int64_t manual_offset_us, int32_t manual_override);
void digitor_live_playback_sync_destroy(DigitorLivePlaybackSync* handle);
void digitor_live_playback_sync_set_manual(DigitorLivePlaybackSync* handle, int64_t offset_us, int32_t override_mode);
void digitor_live_playback_sync_notify_device_change(DigitorLivePlaybackSync* handle);
int32_t digitor_live_playback_sync_refresh(DigitorLivePlaybackSync* handle, int64_t now_us);
int64_t digitor_live_playback_sync_clock(DigitorLivePlaybackSync* handle, int64_t raw_audio_clock_us);
int32_t digitor_live_playback_sync_snapshot(DigitorLivePlaybackSync* handle, DigitorPlaybackSyncSnapshot* out_snapshot);
#ifdef __cplusplus
}
#endif
#endif
