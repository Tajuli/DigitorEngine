#include "digitor/av_sync.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace {

void stress_twelve_hour_audio_clock() {
  digitor::AvSyncClock clock;
  constexpr std::uint32_t sample_rate = 48000;
  constexpr std::uint64_t block_frames = 1024;
  constexpr std::uint64_t total_frames =
      static_cast<std::uint64_t>(sample_rate) * 60u * 60u * 12u;

  std::uint64_t rendered = 0;
  while (rendered < total_frames) {
    const auto step =
        (total_frames - rendered < block_frames) ? total_frames - rendered
                                                 : block_frames;
    clock.advance_audio(step, sample_rate);
    rendered += step;
  }

  assert(clock.position_us() == 12LL * 60LL * 60LL * 1000000LL);
}

void stress_seek_epochs_and_video_queue() {
  digitor::AvSyncClock clock({20000, 50000});
  std::mt19937_64 random(0xD16170ULL);
  std::uniform_int_distribution<std::int64_t> jitter(-80000, 80000);

  for (std::uint64_t epoch = 1; epoch <= 10000; ++epoch) {
    const std::int64_t position = static_cast<std::int64_t>(epoch) * 100000;
    clock.reset(position, epoch);

    std::vector<digitor::TimestampedVideoFrame> queue;
    queue.reserve(17);
    for (std::uint64_t index = 0; index < 16; ++index) {
      queue.push_back({position + jitter(random), epoch, epoch * 100 + index});
    }
    queue.push_back({position, epoch - 1, 0});

    const auto result = clock.select_video_frame(queue);
    if (result.frame) assert(result.frame->seek_epoch == epoch);
  }
}

void stress_preview_export_parity() {
  std::vector<digitor::TimestampedVideoFrame> frames;
  frames.reserve(250000);
  for (std::uint64_t index = 0; index < 250000; ++index) {
    frames.push_back({static_cast<std::int64_t>(index) * 33367, 77,
                      index * 0x9E3779B185EBCA87ULL});
  }

  const auto preview_hash = digitor::av_parity_hash(frames, 77);
  const auto export_hash = digitor::av_parity_hash(frames, 77);
  assert(preview_hash == export_hash);
  assert(preview_hash != digitor::av_parity_hash(frames, 76));
}

void validate_overflow_and_invalid_rate() {
  digitor::AvSyncClock clock;
  bool invalid_rate = false;
  try {
    clock.advance_audio(1, 0);
  } catch (const std::invalid_argument&) {
    invalid_rate = true;
  }
  assert(invalid_rate);

  clock.reset(std::numeric_limits<std::int64_t>::max() - 10, 1);
  bool overflow = false;
  try {
    clock.advance_audio(48000, 48000);
  } catch (const std::overflow_error&) {
    overflow = true;
  }
  assert(overflow);
}

}  // namespace

int main() {
  stress_twelve_hour_audio_clock();
  stress_seek_epochs_and_video_queue();
  stress_preview_export_parity();
  validate_overflow_and_invalid_rate();
  return 0;
}
