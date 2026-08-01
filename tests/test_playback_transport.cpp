#include "digitor/playback_transport.hpp"

#include <cassert>
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
  return 0;
}
