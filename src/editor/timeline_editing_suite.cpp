#include "digitor/timeline_editing_suite.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace digitor {
namespace {
constexpr std::int64_t kMinimumClipUs = 200000;

bool intersects(const TimelineClipModel& a, const TimelineClipModel& b) noexcept {
  return a.start_us < b.start_us + b.duration_us &&
         b.start_us < a.start_us + a.duration_us;
}

TimelineEditResult make_result(TimelineEditKind kind, std::int64_t duration_before_us) {
  TimelineEditResult result{};
  result.kind = kind;
  result.duration_before_us = duration_before_us;
  return result;
}
}  // namespace

ProfessionalTimelineEditor::ProfessionalTimelineEditor(TimelineProjectModel project,
                                                       std::size_t history_limit)
    : project_(std::move(project)), history_(history_limit) {}

const TimelineProjectModel& ProfessionalTimelineEditor::project() const noexcept {
  return project_;
}

const TimelineSelection& ProfessionalTimelineEditor::selection() const noexcept {
  return selection_;
}

const std::vector<TimelineMarker>& ProfessionalTimelineEditor::markers() const noexcept {
  return markers_;
}

std::int64_t ProfessionalTimelineEditor::duration_us() const noexcept {
  return compute_duration();
}

bool ProfessionalTimelineEditor::validate() const noexcept {
  return MultitrackTimeline(project_).validate();
}

TimelineTrackModel* ProfessionalTimelineEditor::find_track(const std::string& id) noexcept {
  for (auto& track : project_.tracks) {
    if (track.id == id) return &track;
  }
  return nullptr;
}

TimelineClipModel* ProfessionalTimelineEditor::find_clip(const std::string& id) noexcept {
  for (auto& track : project_.tracks) {
    for (auto& clip : track.clips) {
      if (clip.id == id) return &clip;
    }
  }
  return nullptr;
}

const TimelineClipModel* ProfessionalTimelineEditor::find_clip(const std::string& id) const noexcept {
  for (const auto& track : project_.tracks) {
    for (const auto& clip : track.clips) {
      if (clip.id == id) return &clip;
    }
  }
  return nullptr;
}

std::vector<std::string> ProfessionalTimelineEditor::linked_ids(
    const TimelineClipModel& clip) const {
  std::vector<std::string> ids;
  if (clip.link_group_id.empty()) {
    ids.push_back(clip.id);
    return ids;
  }
  for (const auto& track : project_.tracks) {
    for (const auto& candidate : track.clips) {
      if (candidate.link_group_id == clip.link_group_id) ids.push_back(candidate.id);
    }
  }
  return ids;
}

std::int64_t ProfessionalTimelineEditor::compute_duration() const noexcept {
  std::int64_t result{};
  for (const auto& track : project_.tracks) {
    for (const auto& clip : track.clips) {
      result = std::max(result, clip.start_us + clip.duration_us);
    }
  }
  return result;
}

bool ProfessionalTimelineEditor::has_overlap(const TimelineTrackModel& track,
                                             const std::string& excluding) const noexcept {
  for (std::size_t i = 0; i < track.clips.size(); ++i) {
    if (track.clips[i].id == excluding) continue;
    for (std::size_t j = i + 1; j < track.clips.size(); ++j) {
      if (track.clips[j].id == excluding) continue;
      if (intersects(track.clips[i], track.clips[j])) return true;
    }
  }
  return false;
}

void ProfessionalTimelineEditor::record_history() {
  if (!transaction_base_) history_.record(project_);
}

bool ProfessionalTimelineEditor::select_clip(const std::string& clip_id, bool additive) {
  if (!find_clip(clip_id)) return false;
  if (!additive) selection_.clip_ids.clear();
  selection_.clip_ids.insert(clip_id);
  selection_.primary_clip_id = clip_id;
  return true;
}

void ProfessionalTimelineEditor::clear_selection() noexcept {
  selection_ = {};
}

bool ProfessionalTimelineEditor::add_marker(TimelineMarker marker) {
  if (marker.id.empty() || marker.time_us < 0 || marker.duration_us < 0) return false;
  if (std::any_of(markers_.begin(), markers_.end(), [&](const auto& value) {
        return value.id == marker.id;
      })) {
    return false;
  }
  markers_.push_back(std::move(marker));
  std::sort(markers_.begin(), markers_.end(),
            [](const auto& a, const auto& b) { return a.time_us < b.time_us; });
  return true;
}

bool ProfessionalTimelineEditor::remove_marker(const std::string& marker_id) {
  const auto it = std::remove_if(markers_.begin(), markers_.end(), [&](const auto& value) {
    return value.id == marker_id;
  });
  if (it == markers_.end()) return false;
  markers_.erase(it, markers_.end());
  return true;
}

TimelineEditResult ProfessionalTimelineEditor::insert_clip(const std::string& track_id,
                                                           TimelineClipModel clip,
                                                           TimelineInsertMode mode) {
  auto result = make_result(mode == TimelineInsertMode::insert ? TimelineEditKind::insert
                                                               : TimelineEditKind::overwrite,
                            compute_duration());
  auto* track = find_track(track_id);
  if (!track || track->locked || clip.id.empty() || clip.start_us < 0 ||
      clip.duration_us < kMinimumClipUs || find_clip(clip.id)) {
    result.diagnostic = "invalid insert";
    return result;
  }

  const auto before = project_;
  record_history();
  const auto end = clip.start_us + clip.duration_us;
  const auto inserted_id = clip.id;

  if (mode == TimelineInsertMode::insert) {
    std::vector<TimelineClipModel> replacement;
    replacement.reserve(track->clips.size() + 1);
    for (auto existing : track->clips) {
      const auto existing_end = existing.start_us + existing.duration_us;
      if (existing.start_us >= clip.start_us) {
        existing.start_us += clip.duration_us;
        replacement.push_back(std::move(existing));
        continue;
      }
      if (existing_end > clip.start_us) {
        const auto left_duration = clip.start_us - existing.start_us;
        const auto right_duration = existing_end - clip.start_us;
        auto right = existing;
        existing.duration_us = left_duration;
        right.id += "-insert-tail";
        right.start_us = end;
        right.source_start_us += left_duration;
        right.duration_us = right_duration;
        if (existing.duration_us >= kMinimumClipUs) replacement.push_back(std::move(existing));
        if (right.duration_us >= kMinimumClipUs) replacement.push_back(std::move(right));
        continue;
      }
      replacement.push_back(std::move(existing));
    }
    track->clips = std::move(replacement);
  } else {
    std::vector<TimelineClipModel> replacement;
    replacement.reserve(track->clips.size() + 1);
    for (auto existing : track->clips) {
      const auto existing_end = existing.start_us + existing.duration_us;
      if (existing_end <= clip.start_us || existing.start_us >= end) {
        replacement.push_back(std::move(existing));
        continue;
      }
      if (existing.start_us < clip.start_us) {
        auto left = existing;
        left.duration_us = clip.start_us - existing.start_us;
        if (left.duration_us >= kMinimumClipUs) replacement.push_back(std::move(left));
      }
      if (existing_end > end) {
        auto right = existing;
        const auto removed = end - existing.start_us;
        right.id += "-overwrite-tail";
        right.start_us = end;
        right.source_start_us += removed;
        right.duration_us = existing_end - end;
        if (right.duration_us >= kMinimumClipUs) replacement.push_back(std::move(right));
      }
    }
    track->clips = std::move(replacement);
  }

  track->clips.push_back(std::move(clip));
  std::sort(track->clips.begin(), track->clips.end(),
            [](const auto& a, const auto& b) { return a.start_us < b.start_us; });
  if (!validate()) {
    project_ = before;
    result.diagnostic = "insert violates timeline constraints";
    return result;
  }
  result.success = true;
  result.affected_clip_ids.push_back(inserted_id);
  result.duration_after_us = compute_duration();
  return result;
}

TimelineEditResult ProfessionalTimelineEditor::lift(const std::string& clip_id,
                                                     bool include_linked) {
  auto result = make_result(TimelineEditKind::lift, compute_duration());
  const auto* clip = find_clip(clip_id);
  if (!clip) {
    result.diagnostic = "clip not found";
    return result;
  }
  auto ids = include_linked ? linked_ids(*clip) : std::vector<std::string>{clip_id};
  const auto before = project_;
  record_history();
  for (auto& track : project_.tracks) {
    if (track.locked) continue;
    track.clips.erase(std::remove_if(track.clips.begin(), track.clips.end(), [&](const auto& value) {
                        return std::find(ids.begin(), ids.end(), value.id) != ids.end();
                      }),
                      track.clips.end());
  }
  if (find_clip(clip_id)) {
    project_ = before;
    result.diagnostic = "clip is on a locked track";
    return result;
  }
  result.success = true;
  result.affected_clip_ids = std::move(ids);
  result.duration_after_us = compute_duration();
  return result;
}

TimelineEditResult ProfessionalTimelineEditor::ripple_delete(const std::string& clip_id,
                                                              bool include_linked) {
  auto result = make_result(TimelineEditKind::ripple_delete, compute_duration());
  const auto* clip = find_clip(clip_id);
  if (!clip) {
    result.diagnostic = "clip not found";
    return result;
  }
  const auto before = project_;
  const auto ripple_start = clip->start_us;
  const auto ripple_duration = clip->duration_us;
  auto removed = lift(clip_id, include_linked);
  if (!removed.success) return result;
  for (auto& track : project_.tracks) {
    if (track.locked) continue;
    for (auto& value : track.clips) {
      if (value.start_us >= ripple_start + ripple_duration) value.start_us -= ripple_duration;
    }
  }
  if (!validate()) {
    project_ = before;
    result.diagnostic = "ripple created invalid project";
    return result;
  }
  result.success = true;
  result.affected_clip_ids = std::move(removed.affected_clip_ids);
  result.duration_after_us = compute_duration();
  return result;
}

TimelineEditResult ProfessionalTimelineEditor::close_gap(const std::string& track_id,
                                                          std::int64_t gap_start_us,
                                                          std::int64_t gap_end_us) {
  auto result = make_result(TimelineEditKind::gap_close, compute_duration());
  auto* track = find_track(track_id);
  if (!track || track->locked || gap_start_us < 0 || gap_end_us <= gap_start_us) {
    result.diagnostic = "invalid gap";
    return result;
  }
  for (const auto& clip : track->clips) {
    if (clip.start_us < gap_end_us && clip.start_us + clip.duration_us > gap_start_us) {
      result.diagnostic = "range is not a gap";
      return result;
    }
  }
  const auto before = project_;
  record_history();
  const auto delta = gap_end_us - gap_start_us;
  for (auto& candidate_track : project_.tracks) {
    if (candidate_track.locked) continue;
    for (auto& clip : candidate_track.clips) {
      if (clip.start_us >= gap_end_us) {
        clip.start_us -= delta;
        result.affected_clip_ids.push_back(clip.id);
      }
    }
  }
  if (!validate()) {
    project_ = before;
    result.diagnostic = "gap close caused overlap";
    return result;
  }
  result.success = true;
  result.duration_after_us = compute_duration();
  return result;
}

TimelineEditResult ProfessionalTimelineEditor::roll_edit(const std::string& left_id,
                                                          const std::string& right_id,
                                                          std::int64_t cut_us) {
  auto result = make_result(TimelineEditKind::roll, compute_duration());
  auto* left = find_clip(left_id);
  auto* right = find_clip(right_id);
  if (!left || !right || left->start_us + left->duration_us != right->start_us ||
      cut_us <= left->start_us || cut_us >= right->start_us + right->duration_us) {
    result.diagnostic = "clips are not adjacent";
    return result;
  }
  const auto before = project_;
  record_history();
  const auto old_right_start = right->start_us;
  const auto right_end = right->start_us + right->duration_us;
  left->duration_us = cut_us - left->start_us;
  right->source_start_us += cut_us - old_right_start;
  right->start_us = cut_us;
  right->duration_us = right_end - cut_us;
  if (left->duration_us < kMinimumClipUs || right->duration_us < kMinimumClipUs ||
      right->source_start_us < 0 ||
      (right->source_duration_us &&
       right->source_start_us + right->duration_us > *right->source_duration_us) ||
      !validate()) {
    project_ = before;
    result.diagnostic = "roll exceeds source bounds";
    return result;
  }
  result.success = true;
  result.affected_clip_ids = {left_id, right_id};
  result.duration_after_us = compute_duration();
  return result;
}

TimelineEditResult ProfessionalTimelineEditor::slip_edit(const std::string& clip_id,
                                                          std::int64_t delta) {
  auto result = make_result(TimelineEditKind::slip, compute_duration());
  auto* clip = find_clip(clip_id);
  if (!clip || clip->locked) {
    result.diagnostic = "clip unavailable";
    return result;
  }
  const auto before = project_;
  record_history();
  clip->source_start_us += delta;
  if (clip->source_start_us < 0 ||
      (clip->source_duration_us &&
       clip->source_start_us + clip->duration_us > *clip->source_duration_us)) {
    project_ = before;
    result.diagnostic = "slip exceeds source bounds";
    return result;
  }
  result.success = true;
  result.affected_clip_ids = {clip_id};
  result.duration_after_us = compute_duration();
  return result;
}

TimelineEditResult ProfessionalTimelineEditor::slide_edit(const std::string& clip_id,
                                                           std::int64_t delta) {
  auto result = make_result(TimelineEditKind::slide, compute_duration());
  TimelineTrackModel* owner{};
  std::size_t index{};
  for (auto& track : project_.tracks) {
    for (std::size_t i = 0; i < track.clips.size(); ++i) {
      if (track.clips[i].id == clip_id) {
        owner = &track;
        index = i;
      }
    }
  }
  if (!owner || owner->locked) {
    result.diagnostic = "slide requires neighbors";
    return result;
  }
  std::sort(owner->clips.begin(), owner->clips.end(),
            [](const auto& a, const auto& b) { return a.start_us < b.start_us; });
  for (std::size_t i = 0; i < owner->clips.size(); ++i) {
    if (owner->clips[i].id == clip_id) index = i;
  }
  if (index == 0 || index + 1 >= owner->clips.size()) {
    result.diagnostic = "slide requires neighbors";
    return result;
  }
  auto& previous = owner->clips[index - 1];
  auto& current = owner->clips[index];
  auto& next = owner->clips[index + 1];
  if (previous.start_us + previous.duration_us != current.start_us ||
      current.start_us + current.duration_us != next.start_us) {
    result.diagnostic = "neighbors are not contiguous";
    return result;
  }
  const auto before = project_;
  record_history();
  previous.duration_us += delta;
  current.start_us += delta;
  next.start_us += delta;
  next.source_start_us += delta;
  next.duration_us -= delta;
  if (previous.duration_us < kMinimumClipUs || next.duration_us < kMinimumClipUs ||
      current.start_us < 0 || next.source_start_us < 0 ||
      (next.source_duration_us &&
       next.source_start_us + next.duration_us > *next.source_duration_us) ||
      !validate()) {
    project_ = before;
    result.diagnostic = "slide exceeds bounds";
    return result;
  }
  result.success = true;
  result.affected_clip_ids = {previous.id, current.id, next.id};
  result.duration_after_us = compute_duration();
  return result;
}

std::optional<CompoundSequence> ProfessionalTimelineEditor::make_compound(
    const std::vector<std::string>& clip_ids,
    const std::string& compound_id,
    const std::string& destination_track_id) {
  if (clip_ids.empty() || compound_id.empty() || find_clip(compound_id)) return std::nullopt;
  auto* destination = find_track(destination_track_id);
  if (!destination || destination->locked) return std::nullopt;

  std::unordered_set<std::string> wanted(clip_ids.begin(), clip_ids.end());
  CompoundSequence sequence;
  sequence.id = compound_id;
  sequence.project.fps = project_.fps;
  std::int64_t start = std::numeric_limits<std::int64_t>::max();
  std::int64_t end{};
  for (const auto& track : project_.tracks) {
    TimelineTrackModel nested = track;
    nested.clips.clear();
    for (const auto& clip : track.clips) {
      if (wanted.contains(clip.id)) {
        nested.clips.push_back(clip);
        start = std::min(start, clip.start_us);
        end = std::max(end, clip.start_us + clip.duration_us);
      }
    }
    if (!nested.clips.empty()) sequence.project.tracks.push_back(std::move(nested));
  }
  if (start == std::numeric_limits<std::int64_t>::max()) return std::nullopt;
  for (auto& track : sequence.project.tracks) {
    for (auto& clip : track.clips) clip.start_us -= start;
  }
  sequence.duration_us = end - start;

  const auto before = project_;
  record_history();
  for (auto& track : project_.tracks) {
    track.clips.erase(std::remove_if(track.clips.begin(), track.clips.end(),
                                     [&](const auto& clip) {
                                       return wanted.contains(clip.id);
                                     }),
                      track.clips.end());
  }
  TimelineClipModel compound{compound_id,
                             TimelineClipType::video,
                             start,
                             sequence.duration_us,
                             0,
                             sequence.duration_us,
                             "",
                             compound_id,
                             false,
                             true,
                             false,
                             1.0,
                             false};
  destination->clips.push_back(compound);
  if (!validate()) {
    project_ = before;
    return std::nullopt;
  }
  return sequence;
}

bool ProfessionalTimelineEditor::break_apart_compound(const CompoundSequence& sequence,
                                                       const std::string& compound_clip_id,
                                                       const std::string& destination_track_id) {
  auto* compound = find_clip(compound_clip_id);
  auto* destination = find_track(destination_track_id);
  if (!compound || !destination || destination->locked) return false;
  const auto offset = compound->start_us;
  const auto before = project_;
  record_history();
  for (auto& track : project_.tracks) {
    track.clips.erase(std::remove_if(track.clips.begin(), track.clips.end(),
                                     [&](const auto& clip) {
                                       return clip.id == compound_clip_id;
                                     }),
                      track.clips.end());
  }
  for (const auto& nested_track : sequence.project.tracks) {
    for (auto clip : nested_track.clips) {
      clip.start_us += offset;
      auto* target = find_track(nested_track.id);
      if (!target || target->locked) target = destination;
      target->clips.push_back(std::move(clip));
    }
  }
  if (!validate()) {
    project_ = before;
    return false;
  }
  return true;
}

bool ProfessionalTimelineEditor::undo() {
  auto value = history_.undo(project_);
  if (!value) return false;
  project_ = std::move(*value);
  return true;
}

bool ProfessionalTimelineEditor::redo() {
  auto value = history_.redo(project_);
  if (!value) return false;
  project_ = std::move(*value);
  return true;
}

void ProfessionalTimelineEditor::begin_transaction() {
  if (!transaction_base_) transaction_base_ = project_;
}

bool ProfessionalTimelineEditor::commit_transaction() {
  if (!transaction_base_) return false;
  history_.record(*transaction_base_);
  transaction_base_.reset();
  return true;
}

void ProfessionalTimelineEditor::rollback_transaction() {
  if (transaction_base_) {
    project_ = std::move(*transaction_base_);
    transaction_base_.reset();
  }
}

}  // namespace digitor
