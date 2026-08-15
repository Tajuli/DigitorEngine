#include "digitor/playback_transport.hpp"
#include "digitor/timeline_audio_session.h"

#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

int main() {
  digitor::PlaybackTransport transport(2'000'000, 0, true);
  const std::vector<digitor::PlaybackFrameTiming> frames{{0, 33333}, {33333, 33333}, {66666, 33333}, {99999, 33333}};

  transport.play(1'000'000);
  assert(transport.position_us(1'030'000) == 30'000);
  assert(transport.select_preview_frame(frames, 1'040'000) == 1);
  assert(transport.select_export_frame(frames, 40'000) == 1);

  transport.pause(1'050'000);
  assert(transport.position_us(1'500'000) == 50'000);
  assert(transport.set_rate(2.0, 1'500'000));
  assert(!transport.set_rate(0.0, 1'500'000));
  transport.play(1'500'000);
  assert(transport.position_us(1'525'000) == 100'000);

  transport.seek(500'000, 1'600'000);
  assert(transport.snapshot(1'600'000).seek_generation == 1);
  transport.play(1'600'000);
  const auto corrected = transport.update_audio_clock(540'000, 1'620'000);
  const auto snap = transport.snapshot(1'620'000);
  assert(corrected >= 535'000 && corrected <= 545'000);
  assert(snap.correction_us <= 5000 && snap.correction_us >= -5000);

  transport.notify_audio_device_changed();
  assert(transport.snapshot(1'620'000).device_generation == 1);
  (void)transport.refresh_audio_device(1'620'000);

  transport.stop();
  assert(transport.position_us(9'000'000) == 0);

  // The editor creates/publishes a timeline before media duration is always
  // known. Playback must still use a monotonic clock in that state; otherwise
  // every Flutter preview poll receives position_us == 0 and Windows displays
  // a valid first frame that never advances.
  DigitorTimelineSessionConfig config{};
  config.sample_rate = 48'000;
  config.channels = 2;
  config.duration_us = 0;

  DigitorTimelineAudioSession* session = nullptr;
  assert(digitor_timeline_session_create(&config, &session) == DIGITOR_RESULT_OK);
  assert(session != nullptr);

  DigitorTimelinePublication publication{};
  publication.revision = 1;
  publication.duration_us = 0;
  publication.video_track_count = 1;
  publication.audio_track_count = 1;
  assert(digitor_timeline_session_publish(session, &publication) == DIGITOR_RESULT_OK);
  assert(digitor_timeline_session_play(session) == DIGITOR_RESULT_OK);

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  DigitorTimelineSessionStatus status{};
  assert(digitor_timeline_session_get_status(session, &status) == DIGITOR_RESULT_OK);
  assert(status.playback_state == DIGITOR_PLAYBACK_PLAYING);
  assert(status.position_us > 0);

  assert(digitor_timeline_session_pause(session) == DIGITOR_RESULT_OK);
  DigitorTimelineSessionStatus paused{};
  assert(digitor_timeline_session_get_status(session, &paused) == DIGITOR_RESULT_OK);
  assert(paused.playback_state == DIGITOR_PLAYBACK_PAUSED);
  const int64_t paused_position = paused.position_us;
  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  assert(digitor_timeline_session_get_status(session, &paused) == DIGITOR_RESULT_OK);
  assert(paused.position_us == paused_position);

  assert(digitor_timeline_session_seek(session, 100'000) == DIGITOR_RESULT_OK);
  DigitorAudioSessionControls controls{};
  controls.master_gain_db = 0.0;
  controls.playback_rate = 2.0;
  controls.preserve_pitch = 1;
  controls.enable_dynamics = 1;
  assert(digitor_timeline_session_set_audio_controls(session, &controls) == DIGITOR_RESULT_OK);
  assert(digitor_timeline_session_play(session) == DIGITOR_RESULT_OK);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  assert(digitor_timeline_session_get_status(session, &status) == DIGITOR_RESULT_OK);
  assert(status.position_us > 110'000);

  // Once authoritative duration arrives, an already-advanced playhead is
  // clamped to the endpoint instead of being reset to zero.
  publication.revision = 2;
  publication.duration_us = 50'000;
  assert(digitor_timeline_session_publish(session, &publication) == DIGITOR_RESULT_OK);
  assert(digitor_timeline_session_get_status(session, &status) == DIGITOR_RESULT_OK);
  assert(status.position_us == 50'000);
  assert(status.playback_state == DIGITOR_PLAYBACK_PAUSED);

  assert(digitor_timeline_session_destroy(session) == DIGITOR_RESULT_OK);
  return 0;
}
