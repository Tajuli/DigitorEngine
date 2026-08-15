#ifndef DIGITOR_TIMELINE_AUDIO_SESSION_H
#define DIGITOR_TIMELINE_AUDIO_SESSION_H

#if defined(_WIN32) && defined(DIGITOR_ENGINE_BUILD) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <stdint.h>
#include "digitor/digitor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DigitorTimelineAudioSession DigitorTimelineAudioSession;

typedef enum DigitorPlaybackState {
    DIGITOR_PLAYBACK_STOPPED = 0,
    DIGITOR_PLAYBACK_PAUSED = 1,
    DIGITOR_PLAYBACK_PLAYING = 2
} DigitorPlaybackState;

typedef struct DigitorTimelineSessionConfig {
    uint32_t sample_rate;
    uint32_t channels;
    int64_t duration_us;
} DigitorTimelineSessionConfig;

typedef struct DigitorTimelinePublication {
    uint64_t revision;
    int64_t duration_us;
    uint32_t video_track_count;
    uint32_t audio_track_count;
} DigitorTimelinePublication;

typedef struct DigitorAudioSessionControls {
    double master_gain_db;
    double playback_rate;
    uint8_t preserve_pitch;
    uint8_t enable_dynamics;
} DigitorAudioSessionControls;

typedef struct DigitorTimelineSessionStatus {
    uint64_t revision;
    uint64_t seek_epoch;
    int64_t position_us;
    int64_t duration_us;
    DigitorPlaybackState playback_state;
    uint32_t sample_rate;
    uint32_t channels;
    double master_gain_db;
    double playback_rate;
    uint8_t preserve_pitch;
    uint8_t enable_dynamics;
} DigitorTimelineSessionStatus;

typedef struct DigitorTimelineSessionTelemetry {
    uint64_t publications;
    uint64_t rejected_publications;
    uint64_t play_commands;
    uint64_t pause_commands;
    uint64_t stop_commands;
    uint64_t seek_commands;
    uint64_t control_updates;
} DigitorTimelineSessionTelemetry;

DIGITOR_API DigitorResult digitor_timeline_session_create(
    const DigitorTimelineSessionConfig* config,
    DigitorTimelineAudioSession** out_session);
DIGITOR_API DigitorResult digitor_timeline_session_destroy(
    DigitorTimelineAudioSession* session);
DIGITOR_API DigitorResult digitor_timeline_session_publish(
    DigitorTimelineAudioSession* session,
    const DigitorTimelinePublication* publication);
DIGITOR_API DigitorResult digitor_timeline_session_attach_media(
    DigitorTimelineAudioSession* session,
    const char* utf8_media_path);
DIGITOR_API DigitorResult digitor_timeline_session_detach_media(
    DigitorTimelineAudioSession* session);
DIGITOR_API DigitorResult digitor_timeline_session_play(DigitorTimelineAudioSession* session);
DIGITOR_API DigitorResult digitor_timeline_session_pause(DigitorTimelineAudioSession* session);
DIGITOR_API DigitorResult digitor_timeline_session_stop(DigitorTimelineAudioSession* session);
DIGITOR_API DigitorResult digitor_timeline_session_seek(
    DigitorTimelineAudioSession* session,
    int64_t position_us);
DIGITOR_API DigitorResult digitor_timeline_session_set_audio_controls(
    DigitorTimelineAudioSession* session,
    const DigitorAudioSessionControls* controls);
DIGITOR_API DigitorResult digitor_timeline_session_get_status(
    DigitorTimelineAudioSession* session,
    DigitorTimelineSessionStatus* out_status);
DIGITOR_API DigitorResult digitor_timeline_session_get_telemetry(
    DigitorTimelineAudioSession* session,
    DigitorTimelineSessionTelemetry* out_telemetry);

#ifdef __cplusplus
}
#endif

#endif
