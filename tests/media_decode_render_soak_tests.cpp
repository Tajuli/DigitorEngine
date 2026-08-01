#include "digitor/async_operation.hpp"
#include "digitor/av_sync.hpp"
#include "digitor/timestamp_normalization.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>

namespace {
std::uint64_t mix(std::uint64_t state, std::uint64_t value) noexcept {
  state ^= value + 0x9e3779b97f4a7c15ULL + (state << 6U) + (state >> 2U);
  return state;
}

std::uint64_t run_soak(bool cancel_early) {
  using namespace digitor;
  constexpr std::size_t kFrames = 180000;
  std::vector<RawVideoTimestamp> raw;
  raw.reserve(kFrames);
  std::int64_t pts = 0;
  for (std::size_t i = 0; i < kFrames; ++i) {
    const auto duration = static_cast<std::int64_t>((i % 5 == 0) ? 41667 : 33333);
    if (i != 0 && i % 45000 == 0) pts += 900000;
    raw.push_back({pts, (i % 17 == 0) ? 0 : duration, static_cast<std::uint64_t>(i)});
    pts += duration;
  }

  const auto normalized = normalize_vfr_timestamps(raw);
  AvSyncClock clock;
  clock.reset(0, 7);
  std::atomic<std::uint64_t> callbacks{0};
  AsyncOperation operation([&](AsyncCompletion) { callbacks.fetch_add(1); });

  std::uint64_t preview_hash = 1469598103934665603ULL;
  std::uint64_t export_hash = 1469598103934665603ULL;
  for (std::size_t i = 0; i < normalized.size(); ++i) {
    if (cancel_early && i == 25000) {
      assert(operation.cancel());
      break;
    }
    const auto& frame = normalized[i];
    const std::uint64_t content = mix(frame.decode_order, static_cast<std::uint64_t>(frame.pts_us));
    preview_hash = mix(preview_hash, content);
    export_hash = mix(export_hash, content);
    const std::vector<TimestampedVideoFrame> queue{{frame.pts_us, 7, content}};
    (void)clock.select_video_frame(queue);
    clock.advance_audio(1600, 48000);
  }

  if (!cancel_early) assert(operation.complete(AsyncCompletion::completed));
  assert(operation.delivered_callbacks() == 1);
  assert(callbacks.load() == 1);
  assert(preview_hash == export_hash);
  return preview_hash;
}
}  // namespace

int main() {
  const auto first = run_soak(false);
  const auto second = run_soak(false);
  assert(first == second);

  for (int i = 0; i < 250; ++i) {
    std::thread worker([] { (void)run_soak(true); });
    worker.join();
  }
  return 0;
}
