#include "digitor/timeline_completion.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace digitor {

bool MultitrackTimeline::remove_track(const std::string& track_id,
                                      TrackRemovalPolicy policy) {
  auto* target = find_track(track_id);
  if (target == nullptr || target->locked) return false;
  if (!target->clips.empty() && policy == TrackRemovalPolicy::reject_if_not_empty) return false;

  std::unordered_set<std::string> removed_clip_ids;
  std::unordered_set<std::string> linked_groups;
  for (const auto& clip : target->clips) {
    if (clip.locked) return false;
    removed_clip_ids.insert(clip.id);
    if (!clip.link_group_id.empty()) linked_groups.insert(clip.link_group_id);
  }

  if (policy == TrackRemovalPolicy::remove_clips_and_linked) {
    for (const auto& track : project_.tracks) {
      for (const auto& clip : track.clips) {
        if (linked_groups.contains(clip.link_group_id)) {
          if (track.locked || clip.locked) return false;
          removed_clip_ids.insert(clip.id);
        }
      }
    }
  }

  const auto before = project_;
  std::erase_if(project_.tracks, [&](const auto& track) { return track.id == track_id; });
  if (policy == TrackRemovalPolicy::remove_clips_and_linked) {
    for (auto& track : project_.tracks) {
      std::erase_if(track.clips, [&](const auto& clip) {
        return removed_clip_ids.contains(clip.id);
      });
    }
  }
  if (!validate()) {
    project_ = before;
    return false;
  }
  return true;
}

bool TimelineCompletionEngine::remove_track(const std::string& track_id,
                                             TrackRemovalPolicy policy) {
  const auto track_it = std::find_if(project_.timeline.tracks.begin(),
                                     project_.timeline.tracks.end(),
                                     [&](const auto& track) { return track.id == track_id; });
  if (track_it == project_.timeline.tracks.end() || track_it->locked) return false;
  if (!track_it->clips.empty() && policy == TrackRemovalPolicy::reject_if_not_empty) return false;

  std::unordered_set<std::string> removed_clip_ids;
  std::unordered_set<std::string> linked_groups;
  for (const auto& clip : track_it->clips) {
    if (clip.locked) return false;
    removed_clip_ids.insert(clip.id);
    if (!clip.link_group_id.empty()) linked_groups.insert(clip.link_group_id);
  }
  if (policy == TrackRemovalPolicy::remove_clips_and_linked) {
    for (const auto& track : project_.timeline.tracks) {
      for (const auto& clip : track.clips) {
        if (linked_groups.contains(clip.link_group_id)) {
          if (track.locked || clip.locked) return false;
          removed_clip_ids.insert(clip.id);
        }
      }
    }
  }

  const auto before = project_;
  MultitrackTimeline timeline(project_.timeline);
  if (!timeline.remove_track(track_id, policy)) return false;
  project_.timeline = timeline.project();

  for (auto& group : project_.track_groups) {
    std::erase(group.track_ids, track_id);
  }
  std::erase_if(project_.track_groups,
                [](const auto& group) { return group.track_ids.empty(); });

  std::erase_if(project_.automation_lanes, [&](const auto& lane) {
    return removed_clip_ids.contains(lane.clip_id);
  });
  std::erase_if(project_.transition_lanes, [&](const auto& lane) {
    return removed_clip_ids.contains(lane.outgoing_clip_id) ||
           removed_clip_ids.contains(lane.incoming_clip_id);
  });

  for (auto& group : project_.multicam_groups) {
    std::vector<std::string> kept_angles;
    std::unordered_map<std::size_t, std::size_t> remap;
    for (std::size_t index = 0; index < group.angle_clip_ids.size(); ++index) {
      if (!removed_clip_ids.contains(group.angle_clip_ids[index])) {
        remap[index] = kept_angles.size();
        kept_angles.push_back(group.angle_clip_ids[index]);
      }
    }
    group.angle_clip_ids = std::move(kept_angles);
    std::vector<MulticamCut> kept_cuts;
    for (const auto& cut : group.cuts) {
      const auto mapped = remap.find(cut.angle_index);
      if (mapped != remap.end()) kept_cuts.push_back({cut.timeline_us, mapped->second});
    }
    group.cuts = std::move(kept_cuts);
  }
  std::erase_if(project_.multicam_groups,
                [](const auto& group) { return group.angle_clip_ids.empty(); });

  if (!validate()) {
    project_ = before;
    return false;
  }
  bump_revision();
  return true;
}

}  // namespace digitor
