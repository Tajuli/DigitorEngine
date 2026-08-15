#include "digitor/timeline_audio_session.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <new>

struct DigitorTimelineAudioSession {
    std::mutex mutex;
    DigitorTimelineSessionStatus status{};
    DigitorTimelineSessionTelemetry telemetry{};
    std::chrono::steady_clock::time_point playback_anchor_time{};
    int64_t playback_anchor_position_us = 0;
    bool playback_anchor_valid = false;
};

namespace {

using PlaybackClock = std::chrono::steady_clock;

bool valid_config(const DigitorTimelineSessionConfig& config) noexcept {
    return config.sample_rate >= 8000 && config.sample_rate <= 384000 &&
           config.channels > 0 && config.channels <= 8 && config.duration_us >= 0;
}

bool valid_controls(const DigitorAudioSessionControls& controls) noexcept {
    return std::isfinite(controls.master_gain_db) && controls.master_gain_db >= -120.0 &&
           controls.master_gain_db <= 24.0 && std::isfinite(controls.playback_rate) &&
           controls.playback_rate >= 0.25 && controls.playback_rate <= 4.0;
}

void anchor_playback_locked(
    DigitorTimelineAudioSession* session,
    PlaybackClock::time_point now) noexcept {
    session->playback_anchor_position_us = session->status.position_us;
    session->playback_anchor_time = now;
    session->playback_anchor_valid = true;
}

void materialize_playback_position_locked(
    DigitorTimelineAudioSession* session,
    PlaybackClock::time_point now) noexcept {
    if (session->status.playback_state != DIGITOR_PLAYBACK_PLAYING ||
        !session->playback_anchor_valid) {
        return;
    }

    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                now - session->playback_anchor_time)
                                .count();
    if (elapsed_us <= 0) return;

    const long double advance =
        static_cast<long double>(elapsed_us) * session->status.playback_rate;
    const long double candidate =
        static_cast<long double>(session->playback_anchor_position_us) + advance;
    const long double max_position =
        static_cast<long double>(std::numeric_limits<int64_t>::max());

    int64_t position_us = candidate >= max_position
                              ? std::numeric_limits<int64_t>::max()
                              : static_cast<int64_t>(candidate);

    // duration_us == 0 explicitly means that media duration is not known yet.
    // The playhead must still advance while the production media provider is
    // resolving metadata, otherwise editor playback remains permanently at t=0.
    if (session->status.duration_us > 0 && position_us >= session->status.duration_us) {
        position_us = session->status.duration_us;
        session->status.position_us = position_us;
        session->status.playback_state = DIGITOR_PLAYBACK_PAUSED;
        session->playback_anchor_valid = false;
        return;
    }

    session->status.position_us = position_us;
}

void materialize_and_reanchor_locked(
    DigitorTimelineAudioSession* session,
    PlaybackClock::time_point now) noexcept {
    materialize_playback_position_locked(session, now);
    if (session->status.playback_state == DIGITOR_PLAYBACK_PLAYING) {
        anchor_playback_locked(session, now);
    }
}

} // namespace

extern "C" {

DigitorResult digitor_timeline_session_create(
    const DigitorTimelineSessionConfig* config,
    DigitorTimelineAudioSession** out_session) {
    if (out_session) *out_session = nullptr;
    if (!config || !out_session || !valid_config(*config)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        auto* session = new DigitorTimelineAudioSession();
        session->status.duration_us = config->duration_us;
        session->status.sample_rate = config->sample_rate;
        session->status.channels = config->channels;
        session->status.playback_state = DIGITOR_PLAYBACK_STOPPED;
        session->status.playback_rate = 1.0;
        session->status.preserve_pitch = 1;
        session->status.enable_dynamics = 1;
        *out_session = session;
        return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_timeline_session_destroy(DigitorTimelineAudioSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        delete session;
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_timeline_session_publish(
    DigitorTimelineAudioSession* session,
    const DigitorTimelinePublication* publication) {
    if (!session || !publication || publication->revision == 0 || publication->duration_us < 0)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        if (publication->revision <= session->status.revision) {
            ++session->telemetry.rejected_publications;
            return DIGITOR_RESULT_RESOURCE_IN_USE;
        }
        const auto now = PlaybackClock::now();
        materialize_and_reanchor_locked(session, now);
        session->status.revision = publication->revision;
        session->status.duration_us = publication->duration_us;
        // Zero is the SDK's explicit "duration not known yet" value. Do not
        // collapse an already valid playhead back to zero while metadata or a
        // registered platform session is still resolving the media duration.
        if (publication->duration_us > 0 &&
            session->status.position_us > publication->duration_us) {
            session->status.position_us = publication->duration_us;
            if (session->status.playback_state == DIGITOR_PLAYBACK_PLAYING) {
                session->status.playback_state = DIGITOR_PLAYBACK_PAUSED;
                session->playback_anchor_valid = false;
            }
        } else if (session->status.playback_state == DIGITOR_PLAYBACK_PLAYING) {
            anchor_playback_locked(session, now);
        }
        ++session->telemetry.publications;
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_timeline_session_play(DigitorTimelineAudioSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        if (session->status.revision == 0) return DIGITOR_RESULT_NOT_INITIALIZED;
        if (session->status.playback_state != DIGITOR_PLAYBACK_PLAYING) {
            session->status.playback_state = DIGITOR_PLAYBACK_PLAYING;
            anchor_playback_locked(session, PlaybackClock::now());
        }
        ++session->telemetry.play_commands;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_pause(DigitorTimelineAudioSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        materialize_playback_position_locked(session, PlaybackClock::now());
        session->status.playback_state = DIGITOR_PLAYBACK_PAUSED;
        session->playback_anchor_valid = false;
        ++session->telemetry.pause_commands;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_stop(DigitorTimelineAudioSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        session->status.playback_state = DIGITOR_PLAYBACK_STOPPED;
        session->status.position_us = 0;
        session->playback_anchor_valid = false;
        ++session->status.seek_epoch;
        ++session->telemetry.stop_commands;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_seek(
    DigitorTimelineAudioSession* session,
    int64_t position_us) {
    if (!session || position_us < 0) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        // A positive duration is authoritative and remains a hard bound. Zero
        // means the duration is not known yet (the state used by editor media
        // open before the production provider publishes full metadata), so
        // seeking/frame stepping must remain valid during that interval.
        if (session->status.duration_us > 0 &&
            position_us > session->status.duration_us)
            return DIGITOR_RESULT_INVALID_ARGUMENT;
        session->status.position_us = position_us;
        if (session->status.playback_state == DIGITOR_PLAYBACK_PLAYING) {
            anchor_playback_locked(session, PlaybackClock::now());
        } else {
            session->playback_anchor_valid = false;
        }
        ++session->status.seek_epoch;
        ++session->telemetry.seek_commands;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_set_audio_controls(
    DigitorTimelineAudioSession* session,
    const DigitorAudioSessionControls* controls) {
    if (!session || !controls || !valid_controls(*controls)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        const auto now = PlaybackClock::now();
        materialize_and_reanchor_locked(session, now);
        session->status.master_gain_db = controls->master_gain_db;
        session->status.playback_rate = controls->playback_rate;
        session->status.preserve_pitch = controls->preserve_pitch ? 1 : 0;
        session->status.enable_dynamics = controls->enable_dynamics ? 1 : 0;
        if (session->status.playback_state == DIGITOR_PLAYBACK_PLAYING) {
            anchor_playback_locked(session, now);
        }
        ++session->telemetry.control_updates;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_get_status(
    DigitorTimelineAudioSession* session,
    DigitorTimelineSessionStatus* out_status) {
    if (out_status) *out_status = {};
    if (!session || !out_status) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        materialize_playback_position_locked(session, PlaybackClock::now());
        *out_status = session->status;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_get_telemetry(
    DigitorTimelineAudioSession* session,
    DigitorTimelineSessionTelemetry* out_telemetry) {
    if (out_telemetry) *out_telemetry = {};
    if (!session || !out_telemetry) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        *out_telemetry = session->telemetry;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

} // extern "C"
