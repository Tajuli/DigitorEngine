#include "digitor/unified_real_media_runtime.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <thread>

using namespace digitor;

namespace {
class FakeDecoder final : public VideoDecoder {
 public:
  std::shared_ptr<VideoFrame> decode(FrameNumber number) override {
    auto frame = std::make_shared<VideoFrame>();
    frame->number = number;
    frame->pts = number * 33333;
    frame->duration = 33333;
    frame->width = 64;
    frame->height = 36;
    NativeMediaSurfaceDescriptor descriptor{};
    descriptor.struct_size = sizeof(descriptor);
    descriptor.platform = NativeMediaPlatform::windows;
    descriptor.handle_type = NativeMediaHandleType::d3d11_texture2d;
    descriptor.pixel_format = NativeMediaPixelFormat::nv12;
    descriptor.width = frame->width;
    descriptor.height = frame->height;
    descriptor.plane_count = 2;
    descriptor.native_handle = static_cast<std::uintptr_t>(number + 1);
    descriptor.timestamp_us = frame->pts;
    frame->native_surface = std::make_shared<NativeMediaSurface>(
        descriptor, std::static_pointer_cast<void>(std::make_shared<int>(1)));
    return frame;
  }
  void seek(std::int64_t pts) override { last_seek = pts; }
  DecoderInfo info() const override {
    return {HardwareDecode::dxva, true, "fake D3D11VA", true,
            NativeMediaHandleType::d3d11_texture2d};
  }
  std::int64_t last_seek{};
};
}  // namespace

int main() {
  static int context;
  std::atomic_uint64_t identity{1};
  auto decoder = std::make_unique<FakeDecoder>();

  ProductionHardwareDecodeOptions decode_options{};
  decode_options.renderer_backend = DIGITOR_RENDERER_D3D12;
  decode_options.render_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  decode_options.require_zero_copy = true;
  decode_options.require_monotonic_timestamps = true;

  auto session = std::make_unique<ProductionHardwareDecodeSession>(
      std::move(decoder),
      [&](const ZeroCopyImportRequest& request, ProcessedGpuFramePtr& output) {
        const auto& surface = request.surface->descriptor();
        GpuFrameMetadata metadata{};
        metadata.width = surface.width;
        metadata.height = surface.height;
        metadata.format = request.output_format;
        metadata.timestamp = surface.timestamp_us;
        output = std::make_shared<ProcessedGpuFrame>(
            &context, DIGITOR_RENDERER_D3D12, metadata, identity.fetch_add(1),
            std::static_pointer_cast<void>(std::make_shared<int>(2)),
            std::make_shared<std::atomic_bool>(true), false);
        return DIGITOR_RESULT_OK;
      },
      decode_options);

  UnifiedRealMediaRuntimeConfig config{};
  config.playback.duration_us = 1000000;
  config.playback.target_fps = 30.0;
  config.playback.prefetch_frames = 2;
  config.playback.maximum_queued_frames = 4;
  config.playback.gpu_memory_budget_bytes = 4 * 1024 * 1024;

  std::atomic_uint64_t presented{};
  UnifiedRealMediaRuntime runtime(
      std::move(session), config,
      [](std::int64_t pts, PlaybackQuality) -> std::optional<FrameNumber> {
        return pts / 33333;
      },
      [&](const ProcessedGpuFramePtr& frame, const UnifiedNativeTextureDescriptor& texture) {
        assert(frame);
        assert(texture.backend == DIGITOR_RENDERER_D3D12);
        assert(texture.width == 64 && texture.height == 36);
        ++presented;
        return DIGITOR_RESULT_OK;
      });

  runtime.play(0);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  for (int i = 0; i < 10 && presented.load() == 0; ++i) {
    (void)runtime.tick(i * 33333);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assert(presented.load() > 0);
  const auto texture = runtime.last_native_texture();
  assert(texture && texture->frame_identity > 0 && texture->generation > 0);
  assert(runtime.decode_qualification().status == HardwareDecodeQualificationStatus::passed);

  std::string diagnostic;
  assert(runtime.seek(200000, 200000, &diagnostic) == DIGITOR_RESULT_OK);
  assert(diagnostic.empty());
  runtime.set_proxy_available(true);
  runtime.set_memory_pressure(PlaybackPressure::critical);
  runtime.play(200000);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  const auto telemetry = runtime.telemetry(233333);
  assert(telemetry.quality == PlaybackQuality::proxy);
  runtime.stop();
  return 0;
}
