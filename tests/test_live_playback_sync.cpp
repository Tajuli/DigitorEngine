#include "digitor/live_playback_sync.hpp"
#include "digitor/live_playback_sync_c_api.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

int main() {
  digitor::LivePlaybackSync sync(5000, true);
  const std::vector<digitor::PlaybackFrameTiming> frames{{0, 33333}, {33333, 33333}, {66666, 33333}};
  assert(sync.compensated_clock_us(0) == 5000);
  assert(sync.select_frame(frames, 30000) == 1);
  auto before = sync.snapshot();
  assert(before.device_change_pending);
  sync.refresh_probe(1000000);
  auto after = sync.snapshot();
  assert(after.probe_generation == 1);
  assert(after.last_probe_time_us == 1000000);
  assert(!after.device_change_pending);
  sync.notify_audio_device_changed();
  assert(sync.snapshot().device_change_pending);

  auto* handle = digitor_live_playback_sync_create(-2000, 1);
  assert(handle != nullptr);
  assert(digitor_live_playback_sync_clock(handle, 10000) == 8000);
  DigitorPlaybackSyncSnapshot snapshot{};
  assert(digitor_live_playback_sync_snapshot(handle, &snapshot) == 1);
  digitor_live_playback_sync_notify_device_change(handle);
  assert(digitor_live_playback_sync_snapshot(handle, &snapshot) == 1);
  assert(snapshot.device_change_pending == 1);
  digitor_live_playback_sync_destroy(handle);
  return 0;
}
