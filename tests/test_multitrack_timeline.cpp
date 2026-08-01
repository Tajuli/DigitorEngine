#include "digitor/multitrack_timeline.hpp"

#include <cassert>

using namespace digitor;

int main() {
  TimelineProjectModel project;
  project.fps = 30;
  project.tracks = {
      {.id = "v1", .name = "Video 1", .type = TimelineTrackType::video},
      {.id = "v2", .name = "Video 2", .type = TimelineTrackType::video},
      {.id = "a1", .name = "Audio 1", .type = TimelineTrackType::audio},
  };
  MultitrackTimeline timeline(project);

  TimelineClipModel video{.id="video", .type=TimelineClipType::video, .start_us=0,
      .duration_us=2000000, .source_start_us=0, .source_duration_us=4000000,
      .link_group_id="link1", .source_media_group_id="media1"};
  TimelineClipModel audio{.id="audio", .type=TimelineClipType::audio, .start_us=0,
      .duration_us=2000000, .source_start_us=0, .source_duration_us=4000000,
      .link_group_id="link1", .source_media_group_id="media1", .embedded_audio=true};
  assert(timeline.add_clip("v1", video));
  assert(timeline.add_clip("a1", audio));
  assert(timeline.validate());

  assert(timeline.move_clip("video", "v2", 1000000, true));
  assert(timeline.project().tracks[1].clips.front().start_us == 1000000);
  assert(timeline.project().tracks[2].clips.front().start_us == 1000000);

  assert(timeline.trim_clip("video", 1200000, 2800000, true));
  assert(timeline.project().tracks[1].clips.front().source_start_us == 200000);
  assert(timeline.project().tracks[2].clips.front().source_start_us == 200000);

  assert(timeline.split_clip("video", 2000000, "video_b", true));
  assert(timeline.validate());

  auto resolved = timeline.resolve(1500000);
  assert(resolved.video_layers.size() == 1);
  assert(resolved.audio_layers.size() == 1);

  TimelineClipModel overlay{.id="overlay", .type=TimelineClipType::overlay,
      .start_us=1200000, .duration_us=500000};
  assert(!timeline.add_clip("v2", overlay));
  assert(timeline.add_clip("v1", overlay));
  resolved = timeline.resolve(1400000);
  assert(resolved.video_layers.size() == 2);

  assert(timeline.unlink_group("link1"));
  assert(timeline.ripple_move("overlay", 3000000));
  assert(timeline.remove_clip("audio", false));
  assert(timeline.validate());

  const auto snapped = timeline.snap(3010000, 20000);
  assert(snapped == 3000000);
  return 0;
}
