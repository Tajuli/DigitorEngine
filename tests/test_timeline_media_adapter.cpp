#include "digitor/production_timeline_gpu_binding.hpp"
#include "digitor/timeline_media_adapter.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void require(bool value, const char* message) {
  if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

digitor::ProcessedGpuFramePtr make_gpu_frame(
    const void* context,
    DigitorRendererBackend backend,
    std::uint32_t width,
    std::uint32_t height,
    std::int64_t timestamp,
    std::uint64_t identity) {
  digitor::GpuFrameMetadata metadata{};
  metadata.width = width;
  metadata.height = height;
  metadata.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  metadata.timestamp = timestamp;
  metadata.color_metadata = "linear-rgba";
  return std::make_shared<digitor::ProcessedGpuFrame>(
      context, backend, std::move(metadata), identity,
      std::static_pointer_cast<void>(std::make_shared<int>(1)),
      std::make_shared<std::atomic_bool>(true), false);
}
}

int main() {
  using namespace digitor;
  int video_decodes = 0;
  int audio_decodes = 0;
  bool preview_delivered = false;
  bool export_delivered = false;
  MediaAdapterCallbacks callbacks;
  callbacks.decode_video = [&](const MediaDecodeRequest& request) -> std::optional<RenderVideoFrame> {
    ++video_decodes;
    require(request.clip_id == "clip", "video clip identity lost");
    require(request.source_time_us == 250000, "source time lost");
    RenderVideoFrame frame{request.width, request.height,
                           std::vector<float>(request.width * request.height * 4U, 0.5F),
                           request.proxy ? "proxy" : "original"};
    return frame;
  };
  callbacks.decode_audio = [&](const MediaDecodeRequest& request, std::size_t frames)
      -> std::optional<RenderAudioBlock> {
    ++audio_decodes;
    require(!request.proxy, "audio must use original media");
    return RenderAudioBlock{48000, std::vector<float>(frames * 2U, 0.25F)};
  };
  callbacks.apply_effects = [](const VideoExecutionLayer&, RenderVideoFrame&) { return true; };
  callbacks.composite = [](const VideoExecutionLayer&, const RenderVideoFrame& input,
                           RenderVideoFrame& output) { output = input; return true; };
  callbacks.deliver_preview = [&](const RenderVideoFrame& frame, const TimelineExecutionPlan&) {
    preview_delivered = frame.provenance == "proxy"; return true;
  };
  callbacks.deliver_export = [&](const RenderVideoFrame& frame, const TimelineExecutionPlan&) {
    export_delivered = frame.provenance == "original"; return true;
  };

  TimelineMediaAdapter adapter({TimelineMediaSource{"clip", "source.mp4", "proxy.mp4", true, 1000000}}, callbacks);
  require(adapter.has_source("clip"), "source registry failed");
  require(adapter.selected_path("clip", TimelineExecutionMode::preview).value() == "proxy.mp4",
          "preview proxy selection failed");
  require(adapter.selected_path("clip", TimelineExecutionMode::export_render).value() == "source.mp4",
          "export original selection failed");

  VideoExecutionLayer video; video.clip_id = "clip"; video.source_time_us = 250000;
  AudioExecutionLayer audio; audio.clip_id = "clip"; audio.source_time_us = 250000;
  auto preview = adapter.make_render_callbacks(TimelineExecutionMode::preview, 2, 2, true);
  const auto preview_frame = preview.decode_video(video, true);
  require(preview_frame && preview_frame->provenance == "proxy", "preview decode bridge failed");
  const auto audio_block = preview.decode_audio(audio, 4);
  require(audio_block && audio_block->interleaved_stereo.size() == 8, "audio decode bridge failed");

  TimelineExecutionPlan preview_plan; preview_plan.mode = TimelineExecutionMode::preview;
  TimelineRenderResult preview_result; preview_result.success = true; preview_result.video = *preview_frame;
  require(adapter.deliver(preview_result, preview_plan), "preview delivery failed");

  auto export_callbacks = adapter.make_render_callbacks(TimelineExecutionMode::export_render, 2, 2, true);
  const auto export_frame = export_callbacks.decode_video(video, true);
  require(export_frame && export_frame->provenance == "original", "export decode bridge failed");
  TimelineExecutionPlan export_plan; export_plan.mode = TimelineExecutionMode::export_render;
  TimelineRenderResult export_result; export_result.success = true; export_result.video = *export_frame;
  require(adapter.deliver(export_result, export_plan), "export delivery failed");
  require(video_decodes == 2 && audio_decodes == 1, "unexpected decode count");
  require(preview_delivered && export_delivered, "sink delivery identity failed");

  static int gpu_context;
  std::uint64_t next_identity = 1;
  ProductionTimelineGpuHost gpu_host{};
  gpu_host.backend = DIGITOR_RENDERER_D3D12;
  gpu_host.context_identity = &gpu_context;
  gpu_host.working_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  gpu_host.device_identity = "test-d3d12-device";
  gpu_host.create_target = [&](std::uint32_t width, std::uint32_t height,
                               std::int64_t timestamp)
      -> std::optional<ProcessedGpuFramePtr> {
    return make_gpu_frame(&gpu_context, DIGITOR_RENDERER_D3D12,
                          width, height, timestamp, next_identity++);
  };
  gpu_host.execute_effects = [&](const VideoExecutionLayer&,
                                 const ProcessedGpuFramePtr& input,
                                 ProcessedGpuFramePtr& output,
                                 std::string&) {
    output = make_gpu_frame(&gpu_context, DIGITOR_RENDERER_D3D12,
                            input->metadata().width, input->metadata().height,
                            input->metadata().timestamp, next_identity++);
    return DIGITOR_RESULT_OK;
  };
  gpu_host.composite_layer = [&](const VideoExecutionLayer&,
                                 const ProcessedGpuFramePtr& input,
                                 const ProcessedGpuFramePtr& target,
                                 ProcessedGpuFramePtr& output,
                                 std::string&) {
    require(input->production_compatible_with(*target),
            "timeline compositor received incompatible GPU frames");
    output = make_gpu_frame(&gpu_context, DIGITOR_RENDERER_D3D12,
                            target->metadata().width, target->metadata().height,
                            target->metadata().timestamp, next_identity++);
    return DIGITOR_RESULT_OK;
  };
  gpu_host.frame_evictable = [](const ProcessedGpuFrame& frame) {
    return frame.ready();
  };

  ProductionTimelineGpuBinding gpu_binding(std::move(gpu_host));
  require(gpu_binding.valid(), "production timeline GPU binding invalid");
  auto bound = gpu_binding.bind(MediaAdapterCallbacks{});
  auto target = bound.create_gpu_target(1920, 1080, 500000);
  require(target && target->gpu_resident() && target->rgba.empty(),
          "GPU target was not created as a zero-readback frame");
  auto effected = *target;
  require(bound.apply_effects(video, effected),
          "native timeline effects binding failed");
  require(bound.composite(video, effected, *target),
          "native timeline compositor binding failed");
  require(bound.gpu_frame_evictable(*target->gpu),
          "GPU completion/eviction binding failed");

  RenderVideoFrame cpu_frame{};
  cpu_frame.width = 1920;
  cpu_frame.height = 1080;
  cpu_frame.rgba.resize(1920U * 1080U * 4U);
  require(!bound.apply_effects(video, cpu_frame),
          "production timeline accepted a CPU frame");

  const auto telemetry = gpu_binding.telemetry();
  require(telemetry.targets_created == 1,
          "unexpected production target count");
  require(telemetry.effect_dispatches == 1,
          "unexpected production effect dispatch count");
  require(telemetry.composite_dispatches == 1,
          "unexpected production composite dispatch count");
  require(telemetry.rejected_cpu_frames == 1,
          "CPU-frame rejection was not recorded");
  return 0;
}
