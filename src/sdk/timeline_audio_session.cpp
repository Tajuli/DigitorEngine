#include "digitor/timeline_audio_session.h"

#include <cmath>
#include <mutex>
#include <new>

struct DigitorTimelineAudioSession {
    std::mutex mutex;
    DigitorTimelineSessionStatus status{};
    DigitorTimelineSessionTelemetry telemetry{};
};

namespace {

bool valid_config(const DigitorTimelineSessionConfig& config) noexcept {
    return config.sample_rate >= 8000 && config.sample_rate <= 384000 &&
           config.channels > 0 && config.channels <= 8 && config.duration_us >= 0;
}

bool valid_controls(const DigitorAudioSessionControls& controls) noexcept {
    return std::isfinite(controls.master_gain_db) && controls.master_gain_db >= -120.0 &&
           controls.master_gain_db <= 24.0 && std::isfinite(controls.playback_rate) &&
           controls.playback_rate >= 0.25 && controls.playback_rate <= 4.0;
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
        session->status.revision = publication->revision;
        session->status.duration_us = publication->duration_us;
        // Zero is the SDK's explicit "duration not known yet" value. Do not
        // collapse an already valid playhead back to zero while metadata or a
        // registered platform session is still resolving the media duration.
        if (publication->duration_us > 0 &&
            session->status.position_us > publication->duration_us)
            session->status.position_us = publication->duration_us;
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
        session->status.playback_state = DIGITOR_PLAYBACK_PLAYING;
        ++session->telemetry.play_commands;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_pause(DigitorTimelineAudioSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        session->status.playback_state = DIGITOR_PLAYBACK_PAUSED;
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
        session->status.master_gain_db = controls->master_gain_db;
        session->status.playback_rate = controls->playback_rate;
        session->status.preserve_pitch = controls->preserve_pitch ? 1 : 0;
        session->status.enable_dynamics = controls->enable_dynamics ? 1 : 0;
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
