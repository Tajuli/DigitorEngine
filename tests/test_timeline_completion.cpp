#include "digitor/timeline_completion.hpp"
#include "digitor/timeline_completion_c_api.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

digitor::TimelineClipModel clip(std::string id,
                                digitor::TimelineClipType type,
                                std::int64_t start,
                                std::int64_t duration) {
  digitor::TimelineClipModel value;
  value.id = std::move(id);
  value.type = type;
  value.start_us = start;
  value.duration_us = duration;
  value.source_duration_us = duration + 10'000'000;
  return value;
}

digitor::TimelineCompletionProject make_project() {
  digitor::TimelineCompletionProject result;
  result.timeline.fps = 30;

  digitor::TimelineTrackModel v1;
  v1.id = "v1";
  v1.name = "Primary";
  v1.type = digitor::TimelineTrackType::video;
  v1.clips.push_back(clip("cam-a", digitor::TimelineClipType::video, 0, 8'000'000));

  digitor::TimelineTrackModel v2;
  v2.id = "v2";
  v2.name = "Angle B";
  v2.type = digitor::TimelineTrackType::video;
  v2.clips.push_back(clip("cam-b", digitor::TimelineClipType::video, 0, 8'000'000));

  digitor::TimelineTrackModel v3;
  v3.id = "v3";
  v3.name = "Incoming";
  v3.type = digitor::TimelineTrackType::video;
  v3.clips.push_back(clip("incoming", digitor::TimelineClipType::overlay, 4'000'000, 4'000'000));

  digitor::TimelineTrackModel a1;
  a1.id = "a1";
  a1.name = "Audio";
  a1.type = digitor::TimelineTrackType::audio;
  a1.clips.push_back(clip("audio-1", digitor::TimelineClipType::audio, 0, 8'000'000));

  result.timeline.tracks = {v1, v2, v3, a1};
  return result;
}

}  // namespace

int main() {
  auto project = make_project();
  digitor::TimelineCompletionEngine engine(project);
  require(engine.validate(), "base project invalid");

  digitor::TimelineTrackGroup group;
  group.id = "av-sync";
  group.track_ids = {"v1", "a1"};
  require(engine.set_track_group(group), "track group rejected");

  digitor::TimelineAutomationLane opacity;
  opacity.id = "opacity-lane";
  opacity.clip_id = "incoming";
  opacity.property = digitor::AutomationProperty::opacity;
  require(opacity.curve.set_keyframe({4'000'000, 0.0, digitor::KeyframeInterpolation::linear}),
          "opacity keyframe one rejected");
  require(opacity.curve.set_keyframe({6'000'000, 1.0, digitor::KeyframeInterpolation::linear}),
          "opacity keyframe two rejected");
  require(engine.set_automation_lane(std::move(opacity)), "automation lane rejected");

  digitor::TimelineTransitionLane transition;
  transition.id = "dissolve";
  transition.outgoing_clip_id = "cam-a";
  transition.incoming_clip_id = "incoming";
  transition.transition.type = digitor::TransitionType::cross_dissolve;
  transition.transition.start_us = 4'000'000;
  transition.transition.duration_us = 2'000'000;
  require(engine.set_transition_lane(transition), "transition lane rejected");

  digitor::MulticamGroup multicam;
  multicam.id = "interview";
  multicam.angle_clip_ids = {"cam-a", "cam-b"};
  multicam.cuts.push_back({0, 0});
  multicam.cuts.push_back({3'000'000, 1});
  require(engine.set_multicam_group(multicam), "multicam group rejected");
  require(engine.switch_multicam_angle("interview", 6'000'000, 0), "multicam cut rejected");

  digitor::NestedTimelineSequence nested;
  nested.id = "nested-sequence";
  nested.project = make_project().timeline;
  require(engine.set_nested_sequence(nested), "nested sequence rejected");

  const auto sample = engine.sample(5'000'000);
  require(sample.render_plan.video_layers.size() == 3U, "video layer resolution mismatch");
  require(sample.render_plan.audio_layers.size() == 1U, "audio layer resolution mismatch");
  require(sample.active_multicam_angles.at("interview") == "cam-b", "multicam active angle mismatch");
  require(std::abs(sample.automation_values.at("opacity-lane") - 0.5) < 0.000001,
          "automation interpolation mismatch");
  require(std::abs(sample.transition_values.at("dissolve").progress - 0.5) < 0.000001,
          "transition progress mismatch");

  const auto nested_frame = engine.resolve_nested("nested-sequence", 1'000'000);
  require(nested_frame.has_value(), "nested resolution missing");
  require(nested_frame->video_layers.size() == 2U, "nested video resolution mismatch");

  const auto revision_before_move = engine.project().revision;
  require(engine.move_track_group("av-sync", 1'000'000), "sync-locked track move failed");
  require(engine.project().revision > revision_before_move, "revision did not advance");

  const std::string archive = engine.serialize();
  require(!archive.empty(), "timeline archive empty");
  const auto decoded = digitor::TimelineCompletionEngine::deserialize(archive);
  require(decoded.has_value(), "timeline archive did not deserialize");
  digitor::TimelineCompletionEngine roundtrip(*decoded);
  require(roundtrip.validate(), "roundtrip project invalid");
  const auto roundtrip_sample = roundtrip.sample(5'000'000);
  require(roundtrip_sample.active_multicam_angles.at("interview") == "cam-b",
          "roundtrip multicam mismatch");

  auto* handle = digitor_timeline_completion_create();
  require(handle != nullptr, "C ABI create failed");
  require(digitor_timeline_completion_load(handle, archive.data(), archive.size()) == 1,
          "C ABI load failed");
  require(digitor_timeline_completion_validate(handle) == 1, "C ABI validation failed");
  DigitorTimelineCompletionSnapshot snapshot{};
  snapshot.struct_size = sizeof(snapshot);
  require(digitor_timeline_completion_sample(handle, 5'000'000, &snapshot) == 1,
          "C ABI sample failed");
  require(snapshot.valid == 1 && snapshot.video_layer_count == 3U && snapshot.audio_layer_count == 1U,
          "C ABI snapshot mismatch");
  const size_t archive_size = digitor_timeline_completion_serialize_size(handle);
  require(archive_size > archive.size(), "C ABI serialize size invalid");
  std::vector<char> output(archive_size);
  require(digitor_timeline_completion_serialize(handle, output.data(), output.size()) == 1,
          "C ABI serialize failed");
  digitor_timeline_completion_destroy(handle);

  auto* editing = digitor_timeline_completion_create();
  require(editing != nullptr, "editing C ABI create failed");
  require(digitor_timeline_completion_add_track(
              editing, "V1", "Video 1", DIGITOR_TIMELINE_TRACK_VIDEO) == 1,
          "editing video track add failed");
  require(digitor_timeline_completion_add_track(
              editing, "A1", "Audio 1", DIGITOR_TIMELINE_TRACK_AUDIO) == 1,
          "editing audio track add failed");
  require(digitor_timeline_completion_add_clip(
              editing, "V1", "video-new", DIGITOR_TIMELINE_CLIP_VIDEO,
              0, 8'000'000, 0, 8'000'000, "media-1", "av-1", 1) == 1,
          "editing video clip add failed");
  require(digitor_timeline_completion_add_clip(
              editing, "A1", "audio-new", DIGITOR_TIMELINE_CLIP_AUDIO,
              0, 8'000'000, 0, 8'000'000, "media-1", "av-1", 0) == 1,
          "editing audio clip add failed");

  DigitorTimelineProjectInfo info{};
  info.struct_size = sizeof(info);
  require(digitor_timeline_completion_project_info(editing, &info) == 1,
          "editing project info failed");
  require(info.valid == 1 && info.clip_count == 2U && info.duration_us == 8'000'000,
          "editing initial project info mismatch");
  const auto revision_before_split = info.revision;

  require(digitor_timeline_completion_split_clip(
              editing, "video-new", 4'000'000, "video-second", 1) == 1,
          "editing linked split failed");
  require(digitor_timeline_completion_project_info(editing, &info) == 1,
          "editing project info after split failed");
  require(info.valid == 1 && info.clip_count == 4U &&
              info.revision > revision_before_split,
          "editing split project info mismatch");

  require(digitor_timeline_completion_remove_clip(
              editing, "video-second", 1) == 1,
          "editing linked delete failed");
  require(digitor_timeline_completion_project_info(editing, &info) == 1,
          "editing project info after delete failed");
  require(info.valid == 1 && info.clip_count == 2U && info.duration_us == 4'000'000,
          "editing delete project info mismatch");
  require(digitor_timeline_completion_validate(editing) == 1,
          "editing C ABI validation failed");
  digitor_timeline_completion_destroy(editing);

  auto corrupted = archive;
  corrupted.replace(0, 8, "BROKEN!!");
  require(!digitor::TimelineCompletionEngine::deserialize(corrupted).has_value(),
          "corrupt archive accepted");

  return 0;
}
