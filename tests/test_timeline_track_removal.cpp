#include "digitor/timeline_completion.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "timeline track removal qualification failed: " << message << '\n';
    std::exit(1);
  }
}

digitor::TimelineClipModel clip(std::string id,
                                digitor::TimelineClipType type,
                                std::int64_t start,
                                std::string link = {}) {
  digitor::TimelineClipModel value;
  value.id = std::move(id);
  value.type = type;
  value.start_us = start;
  value.duration_us = 1'000'000;
  value.source_duration_us = 10'000'000;
  value.link_group_id = std::move(link);
  return value;
}

digitor::TimelineCompletionProject make_project() {
  using namespace digitor;
  TimelineCompletionProject project;
  TimelineTrackModel v1;
  v1.id = "v1";
  v1.type = TimelineTrackType::video;
  v1.clips.push_back(clip("video", TimelineClipType::video, 0, "av"));

  TimelineTrackModel v2;
  v2.id = "v2";
  v2.type = TimelineTrackType::video;

  TimelineTrackModel a1;
  a1.id = "a1";
  a1.type = TimelineTrackType::audio;
  a1.clips.push_back(clip("audio", TimelineClipType::audio, 0, "av"));

  project.timeline.tracks = {v1, v2, a1};
  project.track_groups.push_back({"group", {"v1", "a1"}, true, true});

  TimelineAutomationLane lane;
  lane.id = "opacity";
  lane.clip_id = "video";
  lane.property = AutomationProperty::opacity;
  require(lane.curve.set_keyframe({0, 1.0, KeyframeInterpolation::linear}),
          "automation keyframe setup");
  project.automation_lanes.push_back(lane);

  TimelineTransitionLane transition;
  transition.id = "transition";
  transition.outgoing_clip_id = "video";
  transition.incoming_clip_id = "video";
  transition.transition.start_us = 0;
  transition.transition.duration_us = 500'000;
  project.transition_lanes.push_back(transition);

  MulticamGroup multicam;
  multicam.id = "multicam";
  multicam.angle_clip_ids = {"video"};
  multicam.cuts.push_back({0, 0});
  project.multicam_groups.push_back(multicam);
  return project;
}

}  // namespace

int main() {
  using namespace digitor;

  {
    MultitrackTimeline timeline(make_project().timeline);
    require(timeline.remove_track("v2"), "empty video track should be removable");
    require(timeline.project().tracks.size() == 2U, "empty track count");
    require(timeline.validate(), "empty track removal validity");
  }

  {
    MultitrackTimeline timeline(make_project().timeline);
    require(!timeline.remove_track("v1"), "non-empty track must reject by default");
    require(timeline.remove_track("v1", TrackRemovalPolicy::remove_clips),
            "non-empty video track removal");
    require(timeline.project().tracks.size() == 2U, "video track removed");
    require(timeline.project().tracks.back().clips.size() == 1U,
            "linked audio remains under remove_clips policy");
  }

  {
    TimelineCompletionEngine engine(make_project());
    require(engine.remove_track("v1", TrackRemovalPolicy::remove_clips_and_linked),
            "dependency-safe linked track removal");
    require(engine.validate(), "completion project remains valid");
    require(engine.project().timeline.tracks.size() == 2U, "video track removed");
    require(engine.project().timeline.tracks.back().clips.empty(),
            "linked audio clip removed");
    require(engine.project().automation_lanes.empty(), "automation cleaned");
    require(engine.project().transition_lanes.empty(), "transition cleaned");
    require(engine.project().multicam_groups.empty(), "multicam cleaned");
    require(engine.project().track_groups.size() == 1U, "track group retained");
    require(engine.project().track_groups.front().track_ids.size() == 1U,
            "deleted track removed from group");
    require(engine.project().revision == 1U, "revision incremented once");
  }

  {
    auto project = make_project();
    project.timeline.tracks.back().locked = true;
    TimelineCompletionEngine engine(project);
    require(!engine.remove_track("v1", TrackRemovalPolicy::remove_clips_and_linked),
            "locked linked track must reject cascade");
    require(engine.project().timeline.tracks.size() == 3U, "failed removal rollback");
  }

  std::cout << "timeline track removal qualification passed\n";
  return 0;
}
