#include "digitor/timeline_media_adapter.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
  if (!value) { std::cerr << message << '\n'; std::exit(1); }
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
  return 0;
}
