#pragma once

#include "digitor/production_gpu_export_orchestrator.hpp"

#include <atomic>
#include <cassert>
#include <memory>
#include <optional>
#include <unordered_map>

inline void run_export_m2_contract_cases() {
  using namespace digitor;
  TimelineProjectModel project;
  project.tracks = {{"v1", "Video", TimelineTrackType::video, false, false, false,
                     {{"clip", TimelineClipType::video, 0, 100000, 0, 100000,
                       {}, {}, false, true, false, 1.0, false}}}};
  TimelineRenderExecutor executor(project, {});
  static int context;
  auto make_gpu = [](std::int64_t timestamp, std::uint64_t identity) {
    GpuFrameMetadata metadata{};
    metadata.width = 1920;
    metadata.height = 1080;
    metadata.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
    metadata.timestamp = timestamp;
    metadata.color_metadata = "linear-rgba";
    RenderVideoFrame frame;
    frame.width = 1920;
    frame.height = 1080;
    frame.gpu = std::make_shared<ProcessedGpuFrame>(
        &context, DIGITOR_RENDERER_D3D12, metadata, identity,
        std::static_pointer_cast<void>(std::make_shared<int>(1)),
        std::make_shared<std::atomic_bool>(true), false);
    return frame;
  };

  TimelineRenderCallbacks render_callbacks{};
  render_callbacks.create_gpu_target = [&](std::uint32_t, std::uint32_t,
                                           std::int64_t timestamp) {
    return std::optional<RenderVideoFrame>{make_gpu(timestamp, 1000 + timestamp)};
  };
  render_callbacks.decode_video = [&](const VideoExecutionLayer& layer, bool) {
    return std::optional<RenderVideoFrame>{make_gpu(layer.timeline_us, 2000 + layer.timeline_us)};
  };
  render_callbacks.apply_effects = [](const VideoExecutionLayer&, RenderVideoFrame& frame) {
    return frame.gpu_resident() && frame.rgba.empty();
  };
  render_callbacks.composite = [](const VideoExecutionLayer&,
                                  const RenderVideoFrame& input,
                                  RenderVideoFrame& output) {
    return input.gpu_resident() && output.gpu_resident() &&
           input.rgba.empty() && output.rgba.empty();
  };
  render_callbacks.decode_audio = [](const AudioExecutionLayer&, std::size_t frames) {
    RenderAudioBlock block;
    block.interleaved_stereo.assign(frames * 2U, 0.0F);
    return std::optional<RenderAudioBlock>{std::move(block)};
  };
  render_callbacks.cancelled = [] { return false; };
  TimelineRenderRuntime runtime(executor, render_callbacks, 1, 4);

  ExportRenderSnapshotData data{};
  data.snapshot_identity = 2001;
  data.timeline_revision = 1;
  data.render_revision = 1;
  data.node_graph_revision = 1;
  data.color_pipeline_revision = 1;
  data.audio_revision = 1;
  data.width = 1920;
  data.height = 1080;
  data.working_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  data.fps_num = 30;
  data.fps_den = 1;
  data.duration_us = 100000;
  data.color_metadata = "linear-rgba";
  data.output_path = "m2-output.mp4";
  data.profile.width = 1920;
  data.profile.height = 1080;
  data.profile.fps_num = 30;
  data.profile.fps_den = 1;
  data.profile.prefer_hardware = true;
  data.profile.allow_software_fallback = false;
  data.policy = ExportExecutionPolicy::hardware_required;
  data.renderer_backend = DIGITOR_RENDERER_D3D12;
  data.encoder_backend = EncoderBackend::nvenc;
  auto snapshot = std::make_shared<const ExportRenderSnapshot>(std::move(data));

  std::uint64_t submitted = 0;
  HardwareEncoderCallbacks encoder{};
  encoder.open = [](const HardwareEncodeConfig& config, std::string&) {
    assert(config.require_hardware && config.require_zero_copy);
    assert(config.output_path == "m2-output.mp4");
    assert(!config.profile.allow_software_fallback);
    return DIGITOR_RESULT_OK;
  };
  encoder.submit_gpu_frame = [&](const HardwareEncodeFrame& frame, std::string&) {
    assert(frame.frame && frame.frame->backend() == DIGITOR_RENDERER_D3D12);
    ++submitted;
    return DIGITOR_RESULT_OK;
  };
  encoder.drain = [](std::string&) { return DIGITOR_RESULT_OK; };
  encoder.finalize_atomic = [](std::string&) { return DIGITOR_RESULT_OK; };
  encoder.cancel = [] {};

  std::uint64_t cleanup = 0;
  ProductionGpuExportCallbacks callbacks{};
  callbacks.remove_partial_output = [&] { ++cleanup; };
  callbacks.cancelled = [] { return false; };
  ProductionGpuExportOrchestrator orchestrator(snapshot, runtime, encoder, callbacks);
  const std::vector<ExportFrameTiming> schedule{{0, 33333, true},
                                                 {33333, 33333, false},
                                                 {66666, 33334, false}};
  assert(orchestrator.execute(schedule) == DIGITOR_RESULT_OK);
  const auto telemetry = orchestrator.telemetry();
  assert(telemetry.state == ProductionGpuExportState::completed);
  assert(telemetry.requested_frames == 3 && telemetry.rendered_frames == 3);
  assert(telemetry.encoded_frames == 3 && submitted == 3);
  assert(telemetry.cpu_readbacks == 0 && telemetry.cpu_staging_frames == 0);
  assert(cleanup == 0);

  ProductionGpuExportOrchestrator invalid_schedule(snapshot, runtime, encoder, callbacks);
  assert(invalid_schedule.execute({{10, 10, false}, {10, 10, false}}) !=
         DIGITOR_RESULT_OK);
  assert(invalid_schedule.telemetry().state == ProductionGpuExportState::failed);
  assert(cleanup == 1);

  ProductionGpuExportOrchestrator cancelled(snapshot, runtime, encoder, callbacks);
  cancelled.cancel();
  assert(cancelled.execute(schedule) == DIGITOR_RESULT_RESOURCE_IN_USE);
  assert(cancelled.telemetry().state == ProductionGpuExportState::cancelled);
  assert(cleanup == 2);
}
