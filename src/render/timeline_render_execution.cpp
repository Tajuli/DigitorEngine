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

std::string make_identity(const TimelineExecutionPlan& plan) {
  std::ostringstream out;
  out << plan.timeline_us << ':' << plan.width << 'x' << plan.height << ':'
      << plan.timeline_revision << ':' << plan.render_revision;
  for (const auto& layer : plan.video_layers) {
    out << ":v:" << layer.track_index << ':' << layer.clip_id << ':'
        << layer.source_time_us << ':' << layer.opacity << ':'
        << layer.transition_weight << ':' << layer.cache_key.grade_revision;
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
    if (track.locked) {
      // Locked tracks remain renderable; lock only affects editing.
    }
    for (const auto& clip : track.clips) {
      if (!active(clip, plan.timeline_us)) continue;
      const auto& overrides = find_overrides(overrides_, clip.id);
      const auto source_time = clip.source_start_us + (plan.timeline_us - clip.start_us);
      if (track.type == TimelineTrackType::video) {
        if (track.hidden || !clip.visible) continue;
        const auto opacity = std::clamp(
            overrides.opacity_curve.evaluate(plan.timeline_us, overrides.opacity), 0.0, 1.0);
        VideoExecutionLayer layer;
        layer.clip_id = clip.id;
        layer.track_index = track_index;
        layer.timeline_us = plan.timeline_us;
        layer.source_time_us = source_time;
        layer.opacity = opacity;
        layer.transition_weight = transition_weight(overrides, plan.timeline_us);
        layer.cache_key = {clip.id, source_time, overrides.grade_revision, width, height};
        plan.video_layers.push_back(std::move(layer));
      } else {
        const auto gain = std::max(0.0, overrides.volume_curve.evaluate(
            plan.timeline_us, clip.volume * overrides.volume));
        const auto pan = std::clamp(overrides.pan_curve.evaluate(
            plan.timeline_us, overrides.pan), -1.0, 1.0);
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

bool TimelineRenderExecutor::preview_export_equivalent(
    std::int64_t timeline_us, std::uint32_t width, std::uint32_t height,
    std::uint64_t timeline_revision, std::uint64_t render_revision) const {
  const auto preview = build_plan(TimelineExecutionMode::preview, timeline_us, width,
                                  height, timeline_revision, render_revision);
  const auto export_plan = build_plan(TimelineExecutionMode::export_render, timeline_us,
                                      width, height, timeline_revision, render_revision);
  return preview.identity == export_plan.identity;
}

}  // namespace digitor
