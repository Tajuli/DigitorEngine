#include "digitor/timeline_completion.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace digitor {
namespace {

template <typename T, typename Predicate>
bool replace_by_id(std::vector<T>& values, T value, Predicate predicate) {
  const auto it = std::find_if(values.begin(), values.end(), predicate);
  if (it == values.end()) {
    values.push_back(std::move(value));
  } else {
    *it = std::move(value);
  }
  return true;
}

bool has_track(const TimelineProjectModel& project, const std::string& id) {
  return std::any_of(project.tracks.begin(), project.tracks.end(),
                     [&](const auto& track) { return track.id == id; });
}

bool has_clip(const TimelineProjectModel& project, const std::string& id) {
  for (const auto& track : project.tracks) {
    if (std::any_of(track.clips.begin(), track.clips.end(),
                    [&](const auto& clip) { return clip.id == id; })) {
      return true;
    }
  }
  return false;
}

const TimelineAutomationLane* find_lane(
    const std::vector<TimelineAutomationLane>& lanes,
    const std::string& id) {
  const auto it = std::find_if(lanes.begin(), lanes.end(),
                               [&](const auto& lane) { return lane.id == id; });
  return it == lanes.end() ? nullptr : &*it;
}

bool valid_transition(const TimelineTransitionLane& lane,
                      const TimelineProjectModel& project) {
  return !lane.id.empty() && has_clip(project, lane.outgoing_clip_id) &&
         has_clip(project, lane.incoming_clip_id) &&
         lane.transition.duration_us > 0 && lane.transition.start_us >= 0;
}

std::string active_angle(const MulticamGroup& group, std::int64_t time_us) {
  if (group.angle_clip_ids.empty()) return {};
  std::size_t index{};
  for (const auto& cut : group.cuts) {
    if (cut.timeline_us > time_us) break;
    index = cut.angle_index;
  }
  return index < group.angle_clip_ids.size() ? group.angle_clip_ids[index] : std::string{};
}

void apply_automation(TimelineCompletionSample& sample,
                      const TimelineAutomationLane& lane,
                      std::int64_t timeline_us) {
  const double fallback = lane.property == AutomationProperty::opacity ||
                                  lane.property == AutomationProperty::volume ||
                                  lane.property == AutomationProperty::scale
                              ? 1.0
                              : 0.0;
  const double value = lane.curve.evaluate(timeline_us, fallback);
  sample.automation_values[lane.id] = value;
  if (lane.property == AutomationProperty::opacity) {
    for (auto& layer : sample.render_plan.video_layers) {
      if (layer.clip_id == lane.clip_id) layer.opacity *= value;
    }
  } else if (lane.property == AutomationProperty::volume) {
    for (auto& layer : sample.render_plan.audio_layers) {
      if (layer.clip_id == lane.clip_id) layer.volume *= value;
    }
  }
}

}  // namespace

TimelineCompletionEngine::TimelineCompletionEngine(TimelineCompletionProject project)
    : project_(std::move(project)) {}

const TimelineCompletionProject& TimelineCompletionEngine::project() const noexcept {
  return project_;
}

void TimelineCompletionEngine::bump_revision() noexcept { ++project_.revision; }

bool TimelineCompletionEngine::validate() const noexcept {
  if (!MultitrackTimeline(project_.timeline).validate()) return false;
  std::unordered_set<std::string> ids;
  for (const auto& group : project_.track_groups) {
    if (group.id.empty() || !ids.insert("g:" + group.id).second || group.track_ids.empty()) return false;
    std::unordered_set<std::string> local;
    for (const auto& track_id : group.track_ids) {
      if (!has_track(project_.timeline, track_id) || !local.insert(track_id).second) return false;
    }
  }
  for (const auto& lane : project_.automation_lanes) {
    if (lane.id.empty() || !ids.insert("a:" + lane.id).second || !has_clip(project_.timeline, lane.clip_id)) return false;
  }
  for (const auto& lane : project_.transition_lanes) {
    if (!ids.insert("t:" + lane.id).second || !valid_transition(lane, project_.timeline)) return false;
  }
  for (const auto& group : project_.multicam_groups) {
    if (group.id.empty() || !ids.insert("m:" + group.id).second || group.angle_clip_ids.empty()) return false;
    std::int64_t previous = -1;
    for (const auto& angle : group.angle_clip_ids) if (!has_clip(project_.timeline, angle)) return false;
    for (const auto& cut : group.cuts) {
      if (cut.timeline_us < 0 || cut.timeline_us < previous || cut.angle_index >= group.angle_clip_ids.size()) return false;
      previous = cut.timeline_us;
    }
  }
  for (const auto& sequence : project_.nested_sequences) {
    if (sequence.id.empty() || !ids.insert("n:" + sequence.id).second ||
        !MultitrackTimeline(sequence.project).validate()) return false;
  }
  return true;
}

TimelineCompletionSample TimelineCompletionEngine::sample(std::int64_t timeline_us) const {
  TimelineCompletionSample result;
  result.render_plan = build_render_plan(project_.timeline, timeline_us);
  result.revision = project_.revision;
  for (const auto& lane : project_.automation_lanes) {
    if (lane.enabled) apply_automation(result, lane, timeline_us);
  }
  for (const auto& lane : project_.transition_lanes) {
    if (lane.enabled) result.transition_values[lane.id] = evaluate_transition(lane.transition, timeline_us);
  }
  for (const auto& group : project_.multicam_groups) {
    if (group.enabled) result.active_multicam_angles[group.id] = active_angle(group, timeline_us);
  }
  return result;
}

bool TimelineCompletionEngine::set_track_group(TimelineTrackGroup group) {
  if (group.id.empty() || group.track_ids.empty()) return false;
  std::unordered_set<std::string> unique;
  for (const auto& id : group.track_ids) {
    if (!has_track(project_.timeline, id) || !unique.insert(id).second) return false;
  }
  const std::string id = group.id;
  replace_by_id(project_.track_groups, std::move(group),
                [&](const auto& value) { return value.id == id; });
  bump_revision();
  return true;
}

bool TimelineCompletionEngine::remove_track_group(const std::string& group_id) {
  const auto old_size = project_.track_groups.size();
  std::erase_if(project_.track_groups, [&](const auto& group) { return group.id == group_id; });
  if (project_.track_groups.size() == old_size) return false;
  bump_revision();
  return true;
}

bool TimelineCompletionEngine::move_track_group(const std::string& group_id, std::int64_t delta_us) {
  const auto group_it = std::find_if(project_.track_groups.begin(), project_.track_groups.end(),
                                     [&](const auto& group) { return group.id == group_id; });
  if (group_it == project_.track_groups.end() || !group_it->enabled || !group_it->sync_lock) return false;
  const auto before = project_.timeline;
  for (auto& track : project_.timeline.tracks) {
    if (std::find(group_it->track_ids.begin(), group_it->track_ids.end(), track.id) == group_it->track_ids.end()) continue;
    if (track.locked) {
      project_.timeline = before;
      return false;
    }
    for (auto& clip : track.clips) {
      if (delta_us < 0 && clip.start_us < -delta_us) {
        project_.timeline = before;
        return false;
      }
      clip.start_us += delta_us;
    }
  }
  if (!MultitrackTimeline(project_.timeline).validate()) {
    project_.timeline = before;
    return false;
  }
  bump_revision();
  return true;
}

bool TimelineCompletionEngine::set_automation_lane(TimelineAutomationLane lane) {
  if (lane.id.empty() || !has_clip(project_.timeline, lane.clip_id)) return false;
  const std::string id = lane.id;
  replace_by_id(project_.automation_lanes, std::move(lane),
                [&](const auto& value) { return value.id == id; });
  bump_revision();
  return true;
}

bool TimelineCompletionEngine::remove_automation_lane(const std::string& lane_id) {
  if (!find_lane(project_.automation_lanes, lane_id)) return false;
  std::erase_if(project_.automation_lanes, [&](const auto& lane) { return lane.id == lane_id; });
  bump_revision();
  return true;
}

bool TimelineCompletionEngine::set_transition_lane(TimelineTransitionLane lane) {
  if (!valid_transition(lane, project_.timeline)) return false;
  const std::string id = lane.id;
  replace_by_id(project_.transition_lanes, std::move(lane),
                [&](const auto& value) { return value.id == id; });
  bump_revision();
  return true;
}

bool TimelineCompletionEngine::remove_transition_lane(const std::string& lane_id) {
  const auto old_size = project_.transition_lanes.size();
  std::erase_if(project_.transition_lanes, [&](const auto& lane) { return lane.id == lane_id; });
  if (project_.transition_lanes.size() == old_size) return false;
  bump_revision();
  return true;
}

bool TimelineCompletionEngine::set_multicam_group(MulticamGroup group) {
  if (group.id.empty() || group.angle_clip_ids.empty()) return false;
  for (const auto& id : group.angle_clip_ids) if (!has_clip(project_.timeline, id)) return false;
  std::sort(group.cuts.begin(), group.cuts.end(),
            [](const auto& a, const auto& b) { return a.timeline_us < b.timeline_us; });
  for (const auto& cut : group.cuts) if (cut.timeline_us < 0 || cut.angle_index >= group.angle_clip_ids.size()) return false;
  const std::string id = group.id;
  replace_by_id(project_.multicam_groups, std::move(group),
                [&](const auto& value) { return value.id == id; });
  bump_revision();
  return true;
}

bool TimelineCompletionEngine::switch_multicam_angle(const std::string& group_id,
                                                       std::int64_t timeline_us,
                                                       std::size_t angle_index) {
  auto it = std::find_if(project_.multicam_groups.begin(), project_.multicam_groups.end(),
                         [&](const auto& group) { return group.id == group_id; });
  if (it == project_.multicam_groups.end() || timeline_us < 0 || angle_index >= it->angle_clip_ids.size()) return false;
  auto cut = std::find_if(it->cuts.begin(), it->cuts.end(),
                          [&](const auto& value) { return value.timeline_us == timeline_us; });
  if (cut == it->cuts.end()) it->cuts.push_back({timeline_us, angle_index});
  else cut->angle_index = angle_index;
  std::sort(it->cuts.begin(), it->cuts.end(),
            [](const auto& a, const auto& b) { return a.timeline_us < b.timeline_us; });
  bump_revision();
  return true;
}

bool TimelineCompletionEngine::remove_multicam_group(const std::string& group_id) {
  const auto old_size = project_.multicam_groups.size();
  std::erase_if(project_.multicam_groups, [&](const auto& group) { return group.id == group_id; });
  if (project_.multicam_groups.size() == old_size) return false;
  bump_revision();
  return true;
}

bool TimelineCompletionEngine::set_nested_sequence(NestedTimelineSequence sequence) {
  if (sequence.id.empty() || !MultitrackTimeline(sequence.project).validate()) return false;
  const std::string id = sequence.id;
  replace_by_id(project_.nested_sequences, std::move(sequence),
                [&](const auto& value) { return value.id == id; });
  bump_revision();
  return true;
}

bool TimelineCompletionEngine::remove_nested_sequence(const std::string& sequence_id) {
  const auto old_size = project_.nested_sequences.size();
  std::erase_if(project_.nested_sequences, [&](const auto& sequence) { return sequence.id == sequence_id; });
  if (project_.nested_sequences.size() == old_size) return false;
  bump_revision();
  return true;
}

std::optional<TimelineResolvedFrame> TimelineCompletionEngine::resolve_nested(
    const std::string& sequence_id, std::int64_t timeline_us) const {
  const auto it = std::find_if(project_.nested_sequences.begin(), project_.nested_sequences.end(),
                               [&](const auto& sequence) { return sequence.id == sequence_id; });
  if (it == project_.nested_sequences.end() || timeline_us < 0) return std::nullopt;
  return MultitrackTimeline(it->project).resolve(timeline_us);
}

std::string TimelineCompletionEngine::serialize() const {
  std::ostringstream out;
  const std::string base = serialize_timeline_project(project_.timeline);
  out << "DIGITOR_TIMELINE_COMPLETE 1\n" << project_.revision << '\n' << base.size() << '\n' << base;
  out << project_.track_groups.size() << '\n';
  for (const auto& group : project_.track_groups) {
    out << std::quoted(group.id) << ' ' << group.sync_lock << ' ' << group.enabled << ' ' << group.track_ids.size();
    for (const auto& id : group.track_ids) out << ' ' << std::quoted(id);
    out << '\n';
  }
  out << project_.automation_lanes.size() << '\n';
  for (const auto& lane : project_.automation_lanes) {
    out << std::quoted(lane.id) << ' ' << std::quoted(lane.clip_id) << ' '
        << static_cast<unsigned>(lane.property) << ' ' << lane.enabled << ' ' << lane.curve.keyframes().size();
    for (const auto& key : lane.curve.keyframes()) {
      out << ' ' << key.time_us << ' ' << std::setprecision(17) << key.value << ' '
          << static_cast<unsigned>(key.interpolation);
    }
    out << '\n';
  }
  out << project_.transition_lanes.size() << '\n';
  for (const auto& lane : project_.transition_lanes) {
    out << std::quoted(lane.id) << ' ' << std::quoted(lane.outgoing_clip_id) << ' '
        << std::quoted(lane.incoming_clip_id) << ' ' << static_cast<unsigned>(lane.transition.type) << ' '
        << lane.transition.start_us << ' ' << lane.transition.duration_us << ' ' << lane.enabled << '\n';
  }
  out << project_.multicam_groups.size() << '\n';
  for (const auto& group : project_.multicam_groups) {
    out << std::quoted(group.id) << ' ' << group.enabled << ' ' << group.angle_clip_ids.size();
    for (const auto& id : group.angle_clip_ids) out << ' ' << std::quoted(id);
    out << ' ' << group.cuts.size();
    for (const auto& cut : group.cuts) out << ' ' << cut.timeline_us << ' ' << cut.angle_index;
    out << '\n';
  }
  out << project_.nested_sequences.size() << '\n';
  for (const auto& sequence : project_.nested_sequences) {
    const std::string nested = serialize_timeline_project(sequence.project);
    out << std::quoted(sequence.id) << ' ' << nested.size() << '\n' << nested;
  }
  return out.str();
}

std::optional<TimelineCompletionProject> TimelineCompletionEngine::deserialize(const std::string& text) {
  try {
    std::istringstream in(text);
    std::string magic;
    unsigned version{};
    if (!(in >> magic >> version) || magic != "DIGITOR_TIMELINE_COMPLETE" || version != 1U) return std::nullopt;
    TimelineCompletionProject project;
    std::size_t base_size{};
    if (!(in >> project.revision >> base_size) || base_size > text.size()) return std::nullopt;
    in.get();
    std::string base(base_size, '\0');
    in.read(base.data(), static_cast<std::streamsize>(base_size));
    if (in.gcount() != static_cast<std::streamsize>(base_size)) return std::nullopt;
    auto parsed = deserialize_timeline_project(base);
    if (!parsed) return std::nullopt;
    project.timeline = std::move(*parsed);

    std::size_t count{};
    if (!(in >> count) || count > 10000U) return std::nullopt;
    for (std::size_t i = 0; i < count; ++i) {
      TimelineTrackGroup group;
      std::size_t tracks{};
      if (!(in >> std::quoted(group.id) >> group.sync_lock >> group.enabled >> tracks) || tracks > 10000U) return std::nullopt;
      for (std::size_t j = 0; j < tracks; ++j) {
        std::string id;
        if (!(in >> std::quoted(id))) return std::nullopt;
        group.track_ids.push_back(std::move(id));
      }
      project.track_groups.push_back(std::move(group));
    }

    if (!(in >> count) || count > 100000U) return std::nullopt;
    for (std::size_t i = 0; i < count; ++i) {
      TimelineAutomationLane lane;
      unsigned property{}, interpolation{};
      std::size_t keys{};
      if (!(in >> std::quoted(lane.id) >> std::quoted(lane.clip_id) >> property >> lane.enabled >> keys) ||
          property > static_cast<unsigned>(AutomationProperty::rotation) || keys > 1000000U) return std::nullopt;
      lane.property = static_cast<AutomationProperty>(property);
      for (std::size_t j = 0; j < keys; ++j) {
        TimelineKeyframe key;
        if (!(in >> key.time_us >> key.value >> interpolation) || interpolation > static_cast<unsigned>(KeyframeInterpolation::smooth)) return std::nullopt;
        key.interpolation = static_cast<KeyframeInterpolation>(interpolation);
        if (!lane.curve.set_keyframe(key)) return std::nullopt;
      }
      project.automation_lanes.push_back(std::move(lane));
    }

    if (!(in >> count) || count > 100000U) return std::nullopt;
    for (std::size_t i = 0; i < count; ++i) {
      TimelineTransitionLane lane;
      unsigned type{};
      if (!(in >> std::quoted(lane.id) >> std::quoted(lane.outgoing_clip_id) >>
            std::quoted(lane.incoming_clip_id) >> type >> lane.transition.start_us >>
            lane.transition.duration_us >> lane.enabled) ||
          type > static_cast<unsigned>(TransitionType::wipe_right)) return std::nullopt;
      lane.transition.type = static_cast<TransitionType>(type);
      project.transition_lanes.push_back(std::move(lane));
    }

    if (!(in >> count) || count > 10000U) return std::nullopt;
    for (std::size_t i = 0; i < count; ++i) {
      MulticamGroup group;
      std::size_t angles{}, cuts{};
      if (!(in >> std::quoted(group.id) >> group.enabled >> angles) || angles > 1000U) return std::nullopt;
      for (std::size_t j = 0; j < angles; ++j) {
        std::string id;
        if (!(in >> std::quoted(id))) return std::nullopt;
        group.angle_clip_ids.push_back(std::move(id));
      }
      if (!(in >> cuts) || cuts > 1000000U) return std::nullopt;
      for (std::size_t j = 0; j < cuts; ++j) {
        MulticamCut cut;
        if (!(in >> cut.timeline_us >> cut.angle_index)) return std::nullopt;
        group.cuts.push_back(cut);
      }
      project.multicam_groups.push_back(std::move(group));
    }

    if (!(in >> count) || count > 10000U) return std::nullopt;
    for (std::size_t i = 0; i < count; ++i) {
      NestedTimelineSequence sequence;
      std::size_t nested_size{};
      if (!(in >> std::quoted(sequence.id) >> nested_size) || nested_size > text.size()) return std::nullopt;
      in.get();
      std::string nested(nested_size, '\0');
      in.read(nested.data(), static_cast<std::streamsize>(nested_size));
      if (in.gcount() != static_cast<std::streamsize>(nested_size)) return std::nullopt;
      auto parsed_nested = deserialize_timeline_project(nested);
      if (!parsed_nested) return std::nullopt;
      sequence.project = std::move(*parsed_nested);
      project.nested_sequences.push_back(std::move(sequence));
    }

    TimelineCompletionEngine engine(project);
    if (!engine.validate()) return std::nullopt;
    return project;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace digitor
