#include "digitor/timeline_completion.hpp"

#include <algorithm>

namespace digitor {
namespace {

bool enabled_state(const TimelineTrackModel& track) noexcept {
  return track.type == TimelineTrackType::video ? !track.hidden : !track.muted;
}

void apply_enabled_state(TimelineTrackModel& track, bool enabled) noexcept {
  if (track.type == TimelineTrackType::video) {
    track.hidden = !enabled;
  } else {
    track.muted = !enabled;
  }
}

}  // namespace

bool MultitrackTimeline::track_enabled(const std::string& track_id) const noexcept {
  const auto* track = find_track(track_id);
  return track != nullptr && enabled_state(*track);
}

bool MultitrackTimeline::set_track_enabled(const std::string& track_id, bool enabled) {
  auto* track = find_track(track_id);
  if (track == nullptr || track->locked) return false;
  if (enabled_state(*track) == enabled) return true;
  apply_enabled_state(*track, enabled);
  return true;
}

bool TimelineCompletionEngine::track_enabled(const std::string& track_id) const noexcept {
  const auto it = std::find_if(project_.timeline.tracks.begin(), project_.timeline.tracks.end(),
                               [&](const auto& track) { return track.id == track_id; });
  return it != project_.timeline.tracks.end() && enabled_state(*it);
}

bool TimelineCompletionEngine::set_track_enabled(const std::string& track_id, bool enabled) {
  const auto it = std::find_if(project_.timeline.tracks.begin(), project_.timeline.tracks.end(),
                               [&](const auto& track) { return track.id == track_id; });
  if (it == project_.timeline.tracks.end() || it->locked) return false;
  if (enabled_state(*it) == enabled) return true;
  apply_enabled_state(*it, enabled);
  bump_revision();
  return true;
}

}  // namespace digitor
