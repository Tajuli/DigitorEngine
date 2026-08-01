#include "digitor/production_playback_engine.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <thread>

namespace {
using namespace digitor;

ProcessedGpuFramePtr make_frame(DigitorRendererBackend backend, std::int64_t pts) {
  static int context;
  GpuFrameMetadata metadata{};
  metadata.width = 1920;
  metadata.height = 1080;
  metadata.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  metadata.timestamp = pts;
  auto native = std::static_pointer_cast<void>(std::make_shared<int>(7));
  auto ready = std::make_shared<std::atomic_bool>(true);
  return std::make_shared<ProcessedGpuFrame>(&context, backend, metadata,
                                             static_cast<std::uint64_t>(pts + 1),
                                             std::move(native), std::move(ready), false);
}

bool wait_for_queue(ProductionPlaybackEngine& engine, std::size_t minimum) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (engine.telemetry(0).queued_frames >= minimum) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}
}  // namespace

int main() {
  using namespace digitor;

  std::atomic_uint64_t presented{};
  ProductionPlaybackConfig config{};
  config.duration_us = 2'000'000;
  config.target_fps = 30.0;
  config.prefetch_frames = 4;
  config.maximum_queued_frames = 6;
  config.gpu_memory_budget_bytes = 128ull * 1024ull * 1024ull;

  ProductionPlaybackEngine engine(
      config,
      [](std::int64_t pts, PlaybackQuality quality, std::uint64_t generation)
          -> std::optional<ProductionPlaybackFrame> {
        ProductionPlaybackFrame frame{};
        frame.frame = make_frame(DIGITOR_RENDERER_D3D12, pts);
        frame.pts_us = pts;
        frame.duration_us = 33'333;
        frame.seek_generation = generation;
        frame.estimated_bytes = 16ull * 1024ull * 1024ull;
        frame.quality = quality;
        return frame;
      },
      [&presented](const ProductionPlaybackFrame& frame) {
        assert(frame.frame);
        ++presented;
        return DIGITOR_RESULT_OK;
      });

  engine.play(0);
  assert(wait_for_queue(engine, 2));
  const auto first = engine.tick(40'000);
  assert(first == DIGITOR_RESULT_OK || first == DIGITOR_RESULT_RESOURCE_IN_USE);
  assert(presented.load() <= 1);

  engine.seek(1'000'000, 50'000);
  engine.seek(1'200'000, 51'000);
  assert(wait_for_queue(engine, 1));
  auto after_seek = engine.telemetry(51'000);
  assert(after_seek.seek_requests == 2);
  assert(after_seek.coalesced_seeks <= after_seek.seek_requests);
  assert(after_seek.stale_frames + after_seek.queued_frames >= 1);

  engine.set_proxy_available(true);
  engine.set_thermal_pressure(PlaybackPressure::critical);
  assert(engine.telemetry(52'000).quality == PlaybackQuality::proxy);
  engine.set_thermal_pressure(PlaybackPressure::normal);

  assert(engine.set_rate(2.0, 60'000));
  assert(!engine.set_rate(10.0, 60'000));
  engine.pause(70'000);
  engine.play(80'000);
  engine.notify_audio_device_changed();
  const auto compensated = engine.update_audio_clock(100'000, 90'000);
  (void)compensated;

  engine.stop();
  const auto stopped = engine.telemetry(100'000);
  assert(stopped.transport.state == PlaybackState::stopped);
  assert(stopped.queued_frames == 0);

  ProductionPlaybackConfig strict = config;
  strict.prefetch_frames = 1;
  strict.maximum_queued_frames = 1;
  ProductionPlaybackEngine rejects_cpu(
      strict,
      [](std::int64_t pts, PlaybackQuality, std::uint64_t generation)
          -> std::optional<ProductionPlaybackFrame> {
        return ProductionPlaybackFrame{make_frame(DIGITOR_RENDERER_CPU, pts), pts, 33'333,
                                       generation, 1024, PlaybackQuality::full};
      },
      [](const ProductionPlaybackFrame&) { return DIGITOR_RESULT_OK; });
  rejects_cpu.play(0);
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (rejects_cpu.telemetry(0).cpu_frame_rejections > 0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const auto rejected = rejects_cpu.telemetry(0);
  assert(rejected.cpu_frame_rejections > 0);
  assert(rejected.presented_frames == 0);

  return 0;
}
