#include "digitor/timeline_media_adapter.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
}  // namespace

int main() {
  using namespace digitor;

  int still_decodes = 0;
  int video_decodes = 0;
  int audio_decodes = 0;

  MediaAdapterCallbacks callbacks;
  callbacks.decode_video = [&](const MediaDecodeRequest&) -> std::optional<RenderVideoFrame> {
    ++video_decodes;
    return std::nullopt;
  };
  callbacks.decode_still_image = [&](const MediaDecodeRequest& request)
      -> std::optional<RenderVideoFrame> {
    ++still_decodes;
    require(request.path == "photo.png", "still-image path changed");
    require(request.source_time_us == 0, "still-image source time must remain zero");
    require(!request.proxy, "still images must not select video proxies");
    return RenderVideoFrame{request.width, request.height,
                            std::vector<float>(request.width * request.height * 4U, 1.0F),
                            "still-image"};
  };
  callbacks.decode_audio = [&](const MediaDecodeRequest&, std::size_t)
      -> std::optional<RenderAudioBlock> {
    ++audio_decodes;
    return RenderAudioBlock{};
  };

  TimelineMediaSource source{"photo", "photo.png", "photo-proxy.png", true,
                             5'000'000, TimelineMediaSourceKind::still_image};
  TimelineMediaAdapter adapter({source}, callbacks);

  require(adapter.has_source("photo"), "still-image source was not registered");
  require(adapter.is_still_image("photo"), "still-image source kind was lost");
  require(adapter.selected_path("photo", TimelineExecutionMode::preview).value() == "photo.png",
          "still image incorrectly selected a video proxy");

  VideoExecutionLayer video;
  video.clip_id = "photo";
  video.source_time_us = 3'750'000;
  auto render = adapter.make_render_callbacks(TimelineExecutionMode::preview, 4, 3, true);
  const auto first = render.decode_video(video, true);
  require(first && first->provenance == "still-image", "still-image decode failed");

  video.source_time_us = 4'900'000;
  const auto second = render.decode_video(video, true);
  require(second && second->provenance == "still-image", "still-image duration reuse failed");

  AudioExecutionLayer audio;
  audio.clip_id = "photo";
  require(!render.decode_audio(audio, 128), "still image unexpectedly produced audio");
  require(still_decodes == 2, "still decoder was not used for each requested timeline frame");
  require(video_decodes == 0, "video decoder was used for a still image");
  require(audio_decodes == 0, "audio decoder was used for a still image");
  return 0;
}
