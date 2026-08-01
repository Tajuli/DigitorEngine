#include "digitor/timeline_completion.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "timeline track enablement qualification failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

digitor::TimelineCompletionProject make_project() {
  digitor::TimelineCompletionProject project;
  project.timeline.fps = 30;

  digitor::TimelineTrackModel video;
  video.id = "video-1";
  video.name = "Video 1";
  video.type = digitor::TimelineTrackType::video;
  video.clips.push_back({"video-clip", digitor::TimelineClipType::video, 0, 2'000'000});

  digitor::TimelineTrackModel audio;
  audio.id = "audio-1";
  audio.name = "Audio 1";
  audio.type = digitor::TimelineTrackType::audio;
  audio.clips.push_back({"audio-clip", digitor::TimelineClipType::audio, 0, 2'000'000});

  project.timeline.tracks = {video, audio};
  return project;
}

}  // namespace

int main() {
  digitor::TimelineCompletionEngine engine(make_project());
  require(engine.validate(), "initial project must validate");
  require(engine.track_enabled("video-1"), "video track must start enabled");
  require(engine.track_enabled("audio-1"), "audio track must start enabled");

  const auto initial = engine.sample(500'000);
  require(initial.render_plan.video_layers.size() == 1U, "enabled video track must preview/render");
  require(initial.render_plan.audio_layers.size() == 1U, "enabled audio track must preview/render");

  const auto initial_revision = engine.project().revision;
  require(engine.set_track_enabled("video-1", false), "video track disable must succeed");
  require(!engine.track_enabled("video-1"), "video track must report disabled");
  const auto video_off = engine.sample(500'000);
  require(video_off.render_plan.video_layers.empty(), "disabled video track must not preview or export");
  require(video_off.render_plan.audio_layers.size() == 1U, "video disable must not affect audio");
  require(engine.project().revision == initial_revision + 1U, "state change must bump revision once");

  require(engine.set_track_enabled("audio-1", false), "audio track disable must succeed");
  require(!engine.track_enabled("audio-1"), "audio track must report disabled");
  const auto both_off = engine.sample(500'000);
  require(both_off.render_plan.video_layers.empty(), "video must remain excluded");
  require(both_off.render_plan.audio_layers.empty(), "disabled audio track must not preview or export");

  require(engine.set_track_enabled("video-1", true), "video track enable must succeed");
  require(engine.set_track_enabled("audio-1", true), "audio track enable must succeed");
  const auto both_on = engine.sample(500'000);
  require(both_on.render_plan.video_layers.size() == 1U, "re-enabled video must render");
  require(both_on.render_plan.audio_layers.size() == 1U, "re-enabled audio must render");

  const auto stable_revision = engine.project().revision;
  require(engine.set_track_enabled("audio-1", true), "idempotent enable must succeed");
  require(engine.project().revision == stable_revision, "idempotent enable must not bump revision");

  auto locked_project = make_project();
  locked_project.timeline.tracks.front().locked = true;
  digitor::TimelineCompletionEngine locked(std::move(locked_project));
  require(!locked.set_track_enabled("video-1", false), "locked track state must not change");
  require(locked.track_enabled("video-1"), "locked video track must remain enabled");

  require(engine.set_track_enabled("audio-1", false), "audio disable before archive must succeed");
  const std::string archive = engine.serialize();
  const auto restored_project = digitor::TimelineCompletionEngine::deserialize(archive);
  require(restored_project.has_value(), "serialized project must reload");
  digitor::TimelineCompletionEngine restored(*restored_project);
  require(!restored.track_enabled("audio-1"), "audio off state must survive serialization");
  require(restored.sample(500'000).render_plan.audio_layers.empty(),
          "restored audio off state must affect preview/export plan");

  digitor::MultitrackTimeline raw(make_project().timeline);
  require(raw.set_track_enabled("video-1", false), "raw timeline disable must succeed");
  require(raw.resolve(500'000).video_layers.empty(), "raw resolve must honor video off");
  require(raw.set_track_enabled("video-1", true), "raw timeline enable must succeed");
  require(raw.resolve(500'000).video_layers.size() == 1U, "raw resolve must honor video on");

  return EXIT_SUCCESS;
}
