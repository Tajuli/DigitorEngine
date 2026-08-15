#include "digitor/timeline_audio_session.h"

#include <cassert>
#include <iostream>

int main() {
    DigitorTimelineSessionConfig config{48000, 2, 1000000};
    DigitorTimelineAudioSession* session = nullptr;
    assert(digitor_timeline_session_create(&config, &session) == DIGITOR_RESULT_OK);
    assert(session != nullptr);
    assert(digitor_timeline_session_play(session) == DIGITOR_RESULT_NOT_INITIALIZED);
    assert(digitor_timeline_session_attach_media(nullptr, "unused.mp4") == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_timeline_session_attach_media(session, nullptr) == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_timeline_session_attach_media(session, "") == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_timeline_session_detach_media(nullptr) == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_timeline_session_detach_media(session) == DIGITOR_RESULT_OK);

    DigitorTimelinePublication publication{1, 2000000, 2, 3};
    assert(digitor_timeline_session_publish(session, &publication) == DIGITOR_RESULT_OK);
    assert(digitor_timeline_session_publish(session, &publication) == DIGITOR_RESULT_RESOURCE_IN_USE);
    assert(digitor_timeline_session_play(session) == DIGITOR_RESULT_OK);
    assert(digitor_timeline_session_seek(session, 750000) == DIGITOR_RESULT_OK);

    DigitorAudioSessionControls controls{-3.0, 1.25, 1, 1};
    assert(digitor_timeline_session_set_audio_controls(session, &controls) == DIGITOR_RESULT_OK);

    DigitorTimelineSessionStatus status{};
    assert(digitor_timeline_session_get_status(session, &status) == DIGITOR_RESULT_OK);
    assert(status.revision == 1);
    assert(status.position_us >= 750000);
    assert(status.playback_state == DIGITOR_PLAYBACK_PLAYING);
    assert(status.seek_epoch == 1);
    assert(status.playback_rate == 1.25);

    assert(digitor_timeline_session_pause(session) == DIGITOR_RESULT_OK);
    assert(digitor_timeline_session_stop(session) == DIGITOR_RESULT_OK);
    assert(digitor_timeline_session_get_status(session, &status) == DIGITOR_RESULT_OK);
    assert(status.position_us == 0);
    assert(status.seek_epoch == 2);

    DigitorTimelineSessionTelemetry telemetry{};
    assert(digitor_timeline_session_get_telemetry(session, &telemetry) == DIGITOR_RESULT_OK);
    assert(telemetry.publications == 1);
    assert(telemetry.rejected_publications == 1);
    assert(telemetry.play_commands == 1);
    assert(telemetry.seek_commands == 1);
    assert(telemetry.control_updates == 1);

    controls.playback_rate = 8.0;
    assert(digitor_timeline_session_set_audio_controls(session, &controls) == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_timeline_session_seek(session, 3000000) == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(digitor_timeline_session_destroy(session) == DIGITOR_RESULT_OK);

    DigitorTimelineAudioSession* invalid = reinterpret_cast<DigitorTimelineAudioSession*>(1);
    assert(digitor_timeline_session_create(nullptr, &invalid) == DIGITOR_RESULT_INVALID_ARGUMENT);
    assert(invalid == nullptr);
    std::cout << "timeline audio session C ABI: PASS\n";
    return 0;
}
