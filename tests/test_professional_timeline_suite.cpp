#include "digitor/professional_timeline_suite.hpp"

#include <cassert>
#include <cmath>

using namespace digitor;

int main() {
  AutomationCurve curve;
  assert(curve.set_keyframe({0, 0.0, KeyframeInterpolation::linear}));
  assert(curve.set_keyframe({1'000'000, 1.0, KeyframeInterpolation::smooth}));
  assert(std::abs(curve.evaluate(500'000) - 0.5) < 0.001);
  assert(curve.remove_keyframe(1'000'000));

  const auto transition = evaluate_transition({TransitionType::cross_dissolve, 0, 1'000'000}, 500'000);
  assert(std::abs(transition.outgoing_weight - 0.5) < 0.001);
  assert(std::abs(transition.incoming_weight - 0.5) < 0.001);

  const float a[] = {0.5F, 0.5F, 0.5F, 0.5F};
  const float b[] = {0.25F, 0.25F, 0.25F, 0.25F};
  const auto mix = mix_audio_stereo({{a, 2, 1.0, -1.0, false}, {b, 2, 1.0, 1.0, false}}, 2);
  assert(mix.interleaved_stereo.size() == 4);
  assert(!mix.clipped);

  TimelineProjectModel project;
  project.fps = 30;
  project.tracks = {
      {.id="v1", .name="Video 1", .type=TimelineTrackType::video,
       .clips={{.id="base", .type=TimelineClipType::video, .start_us=0, .duration_us=2'000'000}}},
      {.id="v2", .name="Video 2", .type=TimelineTrackType::video,
       .clips={{.id="overlay", .type=TimelineClipType::overlay, .start_us=500'000, .duration_us=500'000}}},
      {.id="a1", .name="Audio 1", .type=TimelineTrackType::audio,
       .clips={{.id="music", .type=TimelineClipType::audio, .start_us=0, .duration_us=2'000'000, .volume=0.7}}},
  };
  MultitrackTimeline timeline(project);
  assert(timeline.validate());
  const auto plan = build_render_plan(project, 750'000);
  assert(plan.video_layers.size() == 2);
  assert(plan.audio_layers.size() == 1);
  assert(plan.video_layers[0].clip_id == "base");
  assert(plan.video_layers[1].clip_id == "overlay");

  TimelineFrameCache cache(6);
  RenderCacheKey k1{"base", 0, 1, 1, 1};
  RenderCacheKey k2{"base", 1, 1, 1, 1};
  cache.put(k1, {1,2,3,4});
  assert(cache.get(k1).has_value());
  cache.put(k2, {5,6,7,8});
  assert(cache.size_bytes() <= 6);
  cache.invalidate_clip("base");
  assert(cache.size_bytes() == 0);

  RenderJobQueue queue;
  queue.push({1, RenderJobPriority::background, 0, 1});
  queue.push({2, RenderJobPriority::interactive, 0, 1});
  queue.push({3, RenderJobPriority::normal, 0, 2});
  assert(queue.pop()->id == 2);
  queue.cancel_generation(1);
  assert(queue.size() == 1);

  TimelineHistory history(4);
  history.record(project);
  auto changed = project;
  changed.fps = 60;
  const auto undone = history.undo(changed);
  assert(undone && undone->fps == 30);
  const auto redone = history.redo(*undone);
  assert(redone && redone->fps == 60);

  const auto encoded = serialize_timeline_project(project);
  const auto decoded = deserialize_timeline_project(encoded);
  assert(decoded.has_value());
  assert(decoded->fps == 30);
  assert(decoded->tracks.size() == 3);
  assert(decoded->tracks[2].clips[0].id == "music");
  assert(std::abs(decoded->tracks[2].clips[0].volume - 0.7) < 0.0001);
  assert(!deserialize_timeline_project("broken").has_value());
  return 0;
}
