#include "digitor/timeline_render_execution.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace digitor {
namespace {
const ClipExecutionOverrides& find_overrides(
    const std::unordered_map<std::string, ClipExecutionOverrides>& overrides,
    const std::string& clip_id) {
  static const ClipExecutionOverrides defaults{};
  const auto it = overrides.find(clip_id);
  return it == overrides.end() ? defaults : it->second;
}

double transition_weight(const ClipExecutionOverrides& overrides,
                         std::int64_t timeline_us) noexcept {
  double weight = 1.0;
  if (overrides.transition_in.type != TransitionType::none) {
    weight *= evaluate_transition(overrides.transition_in, timeline_us).incoming_weight;
  }
  if (overrides.transition_out.type != TransitionType::none) {
    weight *= evaluate_transition(overrides.transition_out, timeline_us).outgoing_weight;
  }
  return std::clamp(weight, 0.0, 1.0);
}

bool active(const TimelineClipModel& clip, std::int64_t timeline_us) noexcept {
  return clip.duration_us > 0 && timeline_us >= clip.start_us &&
         timeline_us < clip.start_us + clip.duration_us;
}

VisualTransform evaluate_transform(const ClipExecutionOverrides& overrides,
                                   std::int64_t clip_local_time_us) noexcept {
  VisualTransform value = overrides.transform;
  value.position_x = overrides.position_x_curve.evaluate(clip_local_time_us, value.position_x);
  value.position_y = overrides.position_y_curve.evaluate(clip_local_time_us, value.position_y);
  value.scale_x = std::max(0.0, overrides.scale_x_curve.evaluate(clip_local_time_us, value.scale_x));
  value.scale_y = std::max(0.0, overrides.scale_y_curve.evaluate(clip_local_time_us, value.scale_y));
  value.rotation_degrees = overrides.rotation_curve.evaluate(clip_local_time_us, value.rotation_degrees);
  value.anchor_x = overrides.anchor_x_curve.evaluate(clip_local_time_us, value.anchor_x);
  value.anchor_y = overrides.anchor_y_curve.evaluate(clip_local_time_us, value.anchor_y);
  value.crop_left = std::clamp(overrides.crop_left_curve.evaluate(clip_local_time_us, value.crop_left), 0.0, 1.0);
  value.crop_top = std::clamp(overrides.crop_top_curve.evaluate(clip_local_time_us, value.crop_top), 0.0, 1.0);
  value.crop_right = std::clamp(overrides.crop_right_curve.evaluate(clip_local_time_us, value.crop_right), 0.0, 1.0);
  value.crop_bottom = std::clamp(overrides.crop_bottom_curve.evaluate(clip_local_time_us, value.crop_bottom), 0.0, 1.0);
  return value;
}

std::string make_identity(const TimelineExecutionPlan& plan) {
  std::ostringstream out;
  out << plan.timeline_us << ':' << plan.width << 'x' << plan.height << ':'
      << plan.timeline_revision << ':' << plan.render_revision;
  for (const auto& layer : plan.video_layers) {
    const auto& t = layer.transform;
    out << ":v:" << layer.track_index << ':' << layer.clip_id << ':'
        << layer.clip_local_time_us << ':' << layer.source_time_us << ':'
        << layer.opacity << ':' << layer.transition_weight << ':'
        << t.position_x << ':' << t.position_y << ':' << t.scale_x << ':' << t.scale_y
        << ':' << t.rotation_degrees << ':' << t.anchor_x << ':' << t.anchor_y
        << ':' << t.crop_left << ':' << t.crop_top << ':' << t.crop_right << ':'
        << t.crop_bottom << ':' << layer.cache_key.grade_revision;
  }
  for (const auto& layer : plan.audio_layers) {
    out << ":a:" << layer.track_index << ':' << layer.clip_id << ':'
        << layer.source_time_us << ':' << layer.gain << ':' << layer.pan << ':'
        << layer.muted;
  }
  return out.str();
}
}  // namespace

TimelineRenderExecutor::TimelineRenderExecutor(
    TimelineProjectModel project,
    std::unordered_map<std::string, ClipExecutionOverrides> overrides)
    : project_(std::move(project)), overrides_(std::move(overrides)) {}

TimelineExecutionPlan TimelineRenderExecutor::build_plan(
    TimelineExecutionMode mode, std::int64_t timeline_us, std::uint32_t width,
    std::uint32_t height, std::uint64_t timeline_revision,
    std::uint64_t render_revision) const {
  TimelineExecutionPlan plan;
  plan.mode = mode;
  plan.timeline_us = std::max<std::int64_t>(timeline_us, 0);
  plan.width = width;
  plan.height = height;
  plan.timeline_revision = timeline_revision;
  plan.render_revision = render_revision;

  for (std::size_t track_index = 0; track_index < project_.tracks.size(); ++track_index) {
    const auto& track = project_.tracks[track_index];
    for (const auto& clip : track.clips) {
      if (!active(clip, plan.timeline_us)) continue;
      const auto& overrides = find_overrides(overrides_, clip.id);
      const auto clip_local_time = plan.timeline_us - clip.start_us;
      const auto moving_source_time = clip.source_start_us + clip_local_time;
      const auto source_time = overrides.static_visual_source
          ? overrides.static_source_time_us
          : moving_source_time;
      if (track.type == TimelineTrackType::video) {
        if (track.hidden || !clip.visible) continue;
        const auto opacity = std::clamp(
            overrides.opacity_curve.evaluate(clip_local_time, overrides.opacity), 0.0, 1.0);
        VideoExecutionLayer layer;
        layer.clip_id = clip.id;
        layer.track_index = track_index;
        layer.timeline_us = plan.timeline_us;
        layer.clip_local_time_us = clip_local_time;
        layer.source_time_us = source_time;
        layer.opacity = opacity;
        layer.transition_weight = transition_weight(overrides, plan.timeline_us);
        layer.transform = evaluate_transform(overrides, clip_local_time);
        layer.cache_key = {clip.id, source_time, overrides.grade_revision, width, height};
        plan.video_layers.push_back(std::move(layer));
      } else {
        const auto gain = std::max(0.0, overrides.volume_curve.evaluate(
            clip_local_time, clip.volume * overrides.volume));
        const auto pan = std::clamp(overrides.pan_curve.evaluate(
            clip_local_time, overrides.pan), -1.0, 1.0);
        plan.audio_layers.push_back({clip.id, track_index, plan.timeline_us, source_time,
                                     gain, pan, track.muted || clip.muted});
      }
    }
  }

  std::stable_sort(plan.video_layers.begin(), plan.video_layers.end(),
                   [](const auto& a, const auto& b) {
                     return a.track_index < b.track_index;
                   });
  std::stable_sort(plan.audio_layers.begin(), plan.audio_layers.end(),
                   [](const auto& a, const auto& b) {
                     return a.track_index < b.track_index;
                   });
  plan.identity = make_identity(plan);
  return plan;
}

bool TimelineRenderExecutor::preview_export_plan_equivalent(
    std::int64_t timeline_us, std::uint32_t width, std::uint32_t height,
    std::uint64_t timeline_revision, std::uint64_t render_revision) const {
  const auto preview = build_plan(TimelineExecutionMode::preview, timeline_us, width,
                                  height, timeline_revision, render_revision);
  const auto export_plan = build_plan(TimelineExecutionMode::export_render, timeline_us,
                                      width, height, timeline_revision, render_revision);
  return preview.identity == export_plan.identity;
}

}  // namespace digitor
