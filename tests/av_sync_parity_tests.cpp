#include "digitor/av_sync.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
  using namespace digitor;

  AvSyncClock clock;
  clock.reset(0, 7);
  clock.advance_audio(4800, 48000);
  assert(clock.position_us() == 100000);

  std::vector<TimestampedVideoFrame> queue{
      {66666, 6, 1},
      {66666, 7, 11},
      {99999, 7, 12},
  };
  const auto present = clock.select_video_frame(queue);
  assert(present.decision == AvSyncDecision::present);
  assert(present.frame.has_value());
  assert(present.frame->content_hash == 12);
  assert(present.drift_us == 1);

  clock.advance_audio(4800, 48000);
  const auto dropped = clock.select_video_frame(queue);
  assert(dropped.decision == AvSyncDecision::drop);
  assert(dropped.drift_us > 50000);

  clock.reset(33333, 8);
  const auto stale = clock.select_video_frame(queue);
  assert(stale.decision == AvSyncDecision::stale);
  assert(!stale.frame.has_value());

  const std::vector<TimestampedVideoFrame> decoded{
      {0, 9, 101}, {33333, 9, 102}, {66666, 9, 103}, {0, 8, 999}};
  const auto preview_hash = av_parity_hash(decoded, 9);
  const auto export_hash = av_parity_hash(decoded, 9);
  assert(preview_hash == export_hash);
  assert(preview_hash != av_parity_hash(decoded, 8));

  bool invalid_rejected = false;
  try {
    AvSyncClock invalid({50000, 20000});
    (void)invalid;
  } catch (...) {
    invalid_rejected = true;
  }
  assert(invalid_rejected);

  return 0;
}
