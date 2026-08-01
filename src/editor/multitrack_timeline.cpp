#include "digitor/multitrack_timeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace digitor {
namespace {
std::int64_t end_of(const TimelineClipModel& clip) noexcept {
  if (clip.duration_us > 0 && clip.start_us > std::numeric_limits<std::int64_t>::max() - clip.duration_us) {
    return std::numeric_limits<std::int64_t>::max();
  }
  return clip.start_us + std::max<std::int64_t>(clip.duration_us, 0);
}
bool active_at(const TimelineClipModel& clip, std::int64_t position) noexcept {
  return clip.visible && !clip.muted && position >= clip.start_us && position < end_of(clip);
}
}  // namespace

MultitrackTimeline::MultitrackTimeline(TimelineProjectModel project) : project_(std::move(project)) {}
const TimelineProjectModel& MultitrackTimeline::project() const noexcept { return project_; }

std::int64_t MultitrackTimeline::duration_us() const noexcept {
  std::int64_t duration = 0;
  for (const auto& track : project_.tracks) for (const auto& clip : track.clips) duration = std::max(duration, end_of(clip));
  return duration;
}

bool MultitrackTimeline::validate() const noexcept {
  if (project_.fps <= 0) return false;
  std::unordered_set<std::string> track_ids;
  std::unordered_set<std::string> clip_ids;
  for (const auto& track : project_.tracks) {
    if (track.id.empty() || !track_ids.insert(track.id).second || track_has_overlap(track)) return false;
    for (const auto& clip : track.clips) {
      if (clip.id.empty() || !clip_ids.insert(clip.id).second || clip.start_us < 0 || clip.duration_us <= 0 ||
          clip.source_start_us < 0 || !accepts(track, clip)) return false;
      if (clip.source_duration_us && clip.source_start_us + clip.duration_us > *clip.source_duration_us) return false;
    }
  }
  return true;
}

std::int64_t MultitrackTimeline::snap(std::int64_t value, std::int64_t threshold,
                                      const std::string& excluding) const noexcept {
  value = std::max<std::int64_t>(0, value);
  if (threshold < 0) return value;
  std::int64_t best = value;
  std::int64_t best_distance = threshold + 1;
  for (const auto& track : project_.tracks) for (const auto& clip : track.clips) {
    if (clip.id == excluding) continue;
    for (const auto candidate : {clip.start_us, end_of(clip)}) {
      const auto distance = std::llabs(candidate - value);
      if (distance < best_distance) { best = candidate; best_distance = distance; }
    }
  }
  if (project_.fps > 0) {
    const auto frame = 1000000LL / project_.fps;
    const auto candidate = ((value + frame / 2) / frame) * frame;
    const auto distance = std::llabs(candidate - value);
    if (distance < best_distance) { best = candidate; best_distance = distance; }
  }
  return best_distance <= threshold ? best : value;
}

bool MultitrackTimeline::add_track(TimelineTrackModel track) {
  if (track.id.empty() || find_track(track.id)) return false;
  project_.tracks.push_back(std::move(track));
  if (!validate()) { project_.tracks.pop_back(); return false; }
  return true;
}

bool MultitrackTimeline::add_clip(const std::string& track_id, TimelineClipModel clip) {
  auto* track = find_track(track_id);
  if (!track || track->locked || clip.locked || find_clip(clip.id) || !accepts(*track, clip)) return false;
  track->clips.push_back(std::move(clip));
  if (!validate()) { track->clips.pop_back(); return false; }
  return true;
}

bool MultitrackTimeline::remove_clip(const std::string& clip_id, bool remove_linked) {
  const auto* original = find_clip(clip_id);
  if (!original) return false;
  const auto ids = remove_linked ? linked_ids(*original) : std::vector<std::string>{clip_id};
  for (const auto& id : ids) {
    auto* track = containing_track(id);
    auto* clip = find_clip(id);
    if (!track || !clip || track->locked || clip->locked) return false;
  }
  for (auto& track : project_.tracks) {
    track.clips.erase(std::remove_if(track.clips.begin(), track.clips.end(), [&](const auto& clip) {
      return std::find(ids.begin(), ids.end(), clip.id) != ids.end();
    }), track.clips.end());
  }
  return true;
}

bool MultitrackTimeline::link_clips(const std::string& a, const std::string& b, const std::string& group) {
  auto* first = find_clip(a); auto* second = find_clip(b);
  if (!first || !second || first == second || group.empty() || first->locked || second->locked) return false;
  first->link_group_id = group; second->link_group_id = group; return true;
}

bool MultitrackTimeline::unlink_group(const std::string& group) {
  bool changed = false;
  for (auto& track : project_.tracks) for (auto& clip : track.clips) if (clip.link_group_id == group && !clip.locked && !track.locked) {
    clip.link_group_id.clear(); changed = true;
  }
  return changed;
}

bool MultitrackTimeline::move_clip(const std::string& clip_id, const std::string& to_track_id,
                                   std::int64_t start, bool move_linked) {
  auto* clip = find_clip(clip_id); auto* source = containing_track(clip_id); auto* destination = find_track(to_track_id);
  if (!clip || !source || !destination || clip->locked || source->locked || destination->locked || !accepts(*destination, *clip)) return false;
  const auto ids = move_linked ? linked_ids(*clip) : std::vector<std::string>{clip_id};
  const auto delta = std::max<std::int64_t>(0, start) - clip->start_us;
  auto before = project_;
  for (auto& track : project_.tracks) for (auto& item : track.clips) if (std::find(ids.begin(), ids.end(), item.id) != ids.end()) {
    if (item.locked || track.locked) { project_ = std::move(before); return false; }
    item.start_us = std::max<std::int64_t>(0, item.start_us + delta);
  }
  if (source != destination) {
    auto moved = *find_clip(clip_id);
    source->clips.erase(std::remove_if(source->clips.begin(), source->clips.end(), [&](const auto& item){ return item.id == clip_id; }), source->clips.end());
    destination->clips.push_back(std::move(moved));
  }
  if (!validate()) { project_ = std::move(before); return false; }
  return true;
}

bool MultitrackTimeline::trim_clip(const std::string& clip_id, std::int64_t new_start,
                                   std::int64_t new_end, bool trim_linked) {
  auto* clip = find_clip(clip_id); if (!clip || clip->locked || new_start < 0 || new_end - new_start < 200000) return false;
  const auto ids = trim_linked ? linked_ids(*clip) : std::vector<std::string>{clip_id};
  auto before = project_;
  const auto start_delta = new_start - clip->start_us;
  const auto duration_delta = (new_end - new_start) - clip->duration_us;
  for (const auto& id : ids) {
    auto* item = find_clip(id); auto* track = containing_track(id);
    if (!item || !track || item->locked || track->locked) { project_ = std::move(before); return false; }
    item->start_us += start_delta; item->source_start_us += start_delta; item->duration_us += duration_delta;
    if (item->start_us < 0 || item->source_start_us < 0 || item->duration_us < 200000 ||
        (item->source_duration_us && item->source_start_us + item->duration_us > *item->source_duration_us)) {
      project_ = std::move(before); return false;
    }
  }
  if (!validate()) { project_ = std::move(before); return false; }
  return true;
}

bool MultitrackTimeline::split_clip(const std::string& clip_id, std::int64_t position,
                                    const std::string& second_id, bool split_linked) {
  auto* clip = find_clip(clip_id); if (!clip || clip->locked || position <= clip->start_us || position >= end_of(*clip)) return false;
  const auto ids = split_linked ? linked_ids(*clip) : std::vector<std::string>{clip_id};
  auto before = project_;
  std::size_t suffix = 0;
  for (const auto& id : ids) {
    auto* item = find_clip(id); auto* track = containing_track(id);
    if (!item || !track || item->locked || track->locked || position <= item->start_us || position >= end_of(*item)) continue;
    const auto first_duration = position - item->start_us;
    auto second = *item;
    second.id = suffix++ == 0 ? second_id : second_id + "_linked_" + std::to_string(suffix);
    if (second.id.empty() || find_clip(second.id)) { project_ = std::move(before); return false; }
    second.start_us = position; second.duration_us = item->duration_us - first_duration; second.source_start_us += first_duration;
    item->duration_us = first_duration; track->clips.push_back(std::move(second));
  }
  if (!validate()) { project_ = std::move(before); return false; }
  return true;
}

bool MultitrackTimeline::ripple_move(const std::string& clip_id, std::int64_t start) {
  auto* clip = find_clip(clip_id); auto* track = containing_track(clip_id);
  if (!clip || !track || clip->locked || track->locked) return false;
  const auto old = clip->start_us; const auto delta = std::max<std::int64_t>(0, start) - old;
  auto before = project_;
  for (auto& item : track->clips) if (item.id == clip_id || item.start_us >= old) item.start_us = std::max<std::int64_t>(0, item.start_us + delta);
  if (!validate()) { project_ = std::move(before); return false; }
  return true;
}

TimelineResolvedFrame MultitrackTimeline::resolve(std::int64_t position) const noexcept {
  TimelineResolvedFrame result;
  for (const auto& track : project_.tracks) {
    if (track.hidden) continue;
    for (const auto& clip : track.clips) if (active_at(clip, position)) {
      if (track.type == TimelineTrackType::video) result.video_layers.push_back(&clip);
      else if (!track.muted) result.audio_layers.push_back(&clip);
    }
  }
  return result;
}

TimelineTrackModel* MultitrackTimeline::find_track(const std::string& id) noexcept { for (auto& t : project_.tracks) if (t.id == id) return &t; return nullptr; }
const TimelineTrackModel* MultitrackTimeline::find_track(const std::string& id) const noexcept { for (const auto& t : project_.tracks) if (t.id == id) return &t; return nullptr; }
TimelineTrackModel* MultitrackTimeline::containing_track(const std::string& id) noexcept { for (auto& t : project_.tracks) for (auto& c : t.clips) if (c.id == id) return &t; return nullptr; }
const TimelineTrackModel* MultitrackTimeline::containing_track(const std::string& id) const noexcept { for (const auto& t : project_.tracks) for (const auto& c : t.clips) if (c.id == id) return &t; return nullptr; }
TimelineClipModel* MultitrackTimeline::find_clip(const std::string& id) noexcept { auto* t = containing_track(id); if (!t) return nullptr; for (auto& c : t->clips) if (c.id == id) return &c; return nullptr; }
const TimelineClipModel* MultitrackTimeline::find_clip(const std::string& id) const noexcept { const auto* t = containing_track(id); if (!t) return nullptr; for (const auto& c : t->clips) if (c.id == id) return &c; return nullptr; }

bool MultitrackTimeline::accepts(const TimelineTrackModel& track, const TimelineClipModel& clip) const noexcept {
  if (track.type == TimelineTrackType::audio) return clip.type == TimelineClipType::audio;
  return clip.type != TimelineClipType::audio;
}

bool MultitrackTimeline::track_has_overlap(const TimelineTrackModel& track, const std::string& excluding) const noexcept {
  std::vector<const TimelineClipModel*> clips;
  for (const auto& clip : track.clips) if (clip.id != excluding) clips.push_back(&clip);
  std::sort(clips.begin(), clips.end(), [](auto* a, auto* b){ return a->start_us < b->start_us; });
  for (std::size_t i = 1; i < clips.size(); ++i) if (end_of(*clips[i - 1]) > clips[i]->start_us) return true;
  return false;
}

std::vector<std::string> MultitrackTimeline::linked_ids(const TimelineClipModel& clip) const {
  if (clip.link_group_id.empty()) return {clip.id};
  std::vector<std::string> ids;
  for (const auto& track : project_.tracks) for (const auto& item : track.clips) if (item.link_group_id == clip.link_group_id) ids.push_back(item.id);
  return ids.empty() ? std::vector<std::string>{clip.id} : ids;
}

}  // namespace digitor
