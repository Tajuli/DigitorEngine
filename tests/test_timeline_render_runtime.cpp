#include "digitor/timeline_render_runtime.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
}

int main() {
  using namespace digitor;
  TimelineProjectModel project;
  project.tracks = {
      {"v1", "Video 1", TimelineTrackType::video, false, false, false,
       {{"base", TimelineClipType::video, 0, 2'000'000, 100'000, 4'000'000, {}, {}, false, true, false, 1.0, false}}},
      {"v2", "Overlay", TimelineTrackType::video, false, false, false,
       {{"overlay", TimelineClipType::overlay, 0, 2'000'000, 0, 2'000'000, {}, {}, false, true, false, 1.0, false}}},
      {"a1", "Audio", TimelineTrackType::audio, false, false, false,
       {{"audio", TimelineClipType::audio, 0, 2'000'000, 0, 2'000'000, {}, {}, false, true, false, 0.5, false}}}};

  std::unordered_map<std::string, ClipExecutionOverrides> overrides;
  overrides["overlay"].opacity = 0.5;
  TimelineRenderExecutor executor(project, overrides);

  int decode_calls = 0;
  TimelineRenderCallbacks callbacks;
  callbacks.decode_video = [&](const VideoExecutionLayer& layer, bool) {
    ++decode_calls;
    RenderVideoFrame frame;
    frame.width = 2;
    frame.height = 1;
    frame.rgba.assign(8, layer.clip_id == "base" ? 0.2F : 0.4F);
    frame.provenance = "source:" + layer.clip_id;
    return std::optional<RenderVideoFrame>{std::move(frame)};
  };
  callbacks.apply_effects = [](const VideoExecutionLayer&, RenderVideoFrame& frame) {
    for (auto& value : frame.rgba) value += 0.1F;
    return true;
  };
  callbacks.composite = [](const VideoExecutionLayer&, const RenderVideoFrame& input,
                           RenderVideoFrame& output) {
    for (std::size_t i = 0; i < output.rgba.size(); ++i) output.rgba[i] += input.rgba[i];
    output.provenance += "+" + input.provenance;
    return true;
  };
  callbacks.decode_audio = [](const AudioExecutionLayer&, std::size_t frames) {
    RenderAudioBlock block;
    block.interleaved_stereo.assign(frames * 2U, 0.5F);
    return std::optional<RenderAudioBlock>{std::move(block)};
  };
  callbacks.cancelled = [] { return false; };

  TimelineRenderRuntime runtime(executor, callbacks, 1024 * 1024);
  const auto preview = runtime.render(TimelineExecutionMode::preview, 500'000, 2, 1, 1, 1, 4);
  require(preview.success, "preview render failed");
  require(preview.decoded_video_layers == 2, "video layers were not decoded");
  require(preview.cache_misses == 2 && preview.cache_hits == 0, "initial cache accounting failed");
  require(preview.audio.interleaved_stereo.size() == 8, "audio mix was not produced");
  require(std::fabs(preview.video.rgba.front() - 0.55F) < 0.0001F, "composite value incorrect");

  const auto exported = runtime.render(TimelineExecutionMode::export_render, 500'000, 2, 1, 1, 1, 4);
  require(exported.success, "export render failed");
  require(exported.cache_hits == 2 && exported.cache_misses == 0, "cache reuse failed");
  require(preview.plan_identity == exported.plan_identity, "preview/export plan identity differs");
  require(preview.video.rgba == exported.video.rgba, "preview/export pixels differ");
  require(decode_calls == 2, "cached export decoded video again");

  runtime.invalidate_clip("overlay");
  const auto invalidated = runtime.render(TimelineExecutionMode::preview, 500'000, 2, 1, 1, 1, 4);
  require(invalidated.success, "render after invalidation failed");
  require(invalidated.cache_hits == 1 && invalidated.cache_misses == 1, "clip invalidation failed");
  return 0;
}
