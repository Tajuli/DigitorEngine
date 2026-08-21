#include "digitor/timeline_completion_c_api.h"

#include "digitor/timeline_completion.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>

struct DigitorTimelineCompletionHandle {
  DigitorTimelineCompletionHandle()
      : engine(digitor::TimelineCompletionProject{}) {}

  digitor::TimelineCompletionEngine engine;
};

namespace {

bool valid_text(const char* value) noexcept {
  return value != nullptr && value[0] != '\0';
}

std::optional<digitor::TimelineTrackType> track_type(DigitorTimelineTrackKind kind) {
  switch (kind) {
    case DIGITOR_TIMELINE_TRACK_VIDEO:
      return digitor::TimelineTrackType::video;
    case DIGITOR_TIMELINE_TRACK_AUDIO:
      return digitor::TimelineTrackType::audio;
  }
  return std::nullopt;
}

std::optional<digitor::TimelineClipType> clip_type(DigitorTimelineClipKind kind) {
  switch (kind) {
    case DIGITOR_TIMELINE_CLIP_VIDEO:
      return digitor::TimelineClipType::video;
    case DIGITOR_TIMELINE_CLIP_IMAGE:
      return digitor::TimelineClipType::image;
    case DIGITOR_TIMELINE_CLIP_OVERLAY:
      return digitor::TimelineClipType::overlay;
    case DIGITOR_TIMELINE_CLIP_TEXT:
      return digitor::TimelineClipType::text;
    case DIGITOR_TIMELINE_CLIP_AUDIO:
      return digitor::TimelineClipType::audio;
  }
  return std::nullopt;
}

void prune_clip_metadata(digitor::TimelineCompletionProject& project) {
  std::unordered_set<std::string> clip_ids;
  for (const auto& track : project.timeline.tracks) {
    for (const auto& clip : track.clips) clip_ids.insert(clip.id);
  }
  const auto missing = [&](const std::string& id) {
    return clip_ids.find(id) == clip_ids.end();
  };
  std::erase_if(project.automation_lanes,
                [&](const auto& lane) { return missing(lane.clip_id); });
  std::erase_if(project.transition_lanes, [&](const auto& lane) {
    return missing(lane.outgoing_clip_id) || missing(lane.incoming_clip_id);
  });
  std::erase_if(project.multicam_groups, [&](const auto& group) {
    return std::any_of(group.angle_clip_ids.begin(), group.angle_clip_ids.end(), missing);
  });
}

template <typename Mutation>
bool mutate_timeline(DigitorTimelineCompletionHandle* handle, Mutation&& mutation,
                     bool prune_metadata = false) {
  if (handle == nullptr) return false;
  auto project = handle->engine.project();
  digitor::MultitrackTimeline timeline(project.timeline);
  if (!mutation(timeline)) return false;
  project.timeline = timeline.project();
  if (prune_metadata) prune_clip_metadata(project);
  ++project.revision;
  digitor::TimelineCompletionEngine next(std::move(project));
  if (!next.validate()) return false;
  handle->engine = std::move(next);
  return true;
}

}  // namespace

extern "C" {

DigitorTimelineCompletionHandle* digitor_timeline_completion_create(void) {
  try {
    return new DigitorTimelineCompletionHandle();
  } catch (...) {
    return nullptr;
  }
}

void digitor_timeline_completion_destroy(DigitorTimelineCompletionHandle* handle) {
  delete handle;
}

int digitor_timeline_completion_load(DigitorTimelineCompletionHandle* handle,
                                     const char* serialized_project,
                                     size_t serialized_size) {
  if (handle == nullptr || serialized_project == nullptr || serialized_size == 0U) return 0;
  try {
    const std::string text(serialized_project, serialized_size);
    auto project = digitor::TimelineCompletionEngine::deserialize(text);
    if (!project) return 0;
    handle->engine = digitor::TimelineCompletionEngine(std::move(*project));
    return handle->engine.validate() ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

int digitor_timeline_completion_add_track(
    DigitorTimelineCompletionHandle* handle,
    const char* track_id,
    const char* track_name,
    DigitorTimelineTrackKind kind) {
  if (!valid_text(track_id)) return 0;
  try {
    const auto type = track_type(kind);
    if (!type) return 0;
    digitor::TimelineTrackModel track;
    track.id = track_id;
    track.name = valid_text(track_name) ? track_name : track_id;
    track.type = *type;
    return mutate_timeline(handle, [&](auto& timeline) {
      return timeline.add_track(std::move(track));
    }) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

int digitor_timeline_completion_add_clip(
    DigitorTimelineCompletionHandle* handle,
    const char* track_id,
    const char* clip_id,
    DigitorTimelineClipKind kind,
    int64_t start_us,
    int64_t duration_us,
    int64_t source_start_us,
    int64_t source_duration_us,
    const char* source_media_group_id,
    const char* link_group_id,
    int embedded_audio) {
  if (!valid_text(track_id) || !valid_text(clip_id)) return 0;
  try {
    const auto type = clip_type(kind);
    if (!type) return 0;
    digitor::TimelineClipModel clip;
    clip.id = clip_id;
    clip.type = *type;
    clip.start_us = start_us;
    clip.duration_us = duration_us;
    clip.source_start_us = source_start_us;
    if (source_duration_us > 0) clip.source_duration_us = source_duration_us;
    if (valid_text(source_media_group_id)) clip.source_media_group_id = source_media_group_id;
    if (valid_text(link_group_id)) clip.link_group_id = link_group_id;
    clip.embedded_audio = embedded_audio != 0;
    return mutate_timeline(handle, [&](auto& timeline) {
      return timeline.add_clip(track_id, std::move(clip));
    }) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

int digitor_timeline_completion_split_clip(
    DigitorTimelineCompletionHandle* handle,
    const char* clip_id,
    int64_t position_us,
    const char* second_clip_id,
    int split_linked) {
  if (!valid_text(clip_id) || !valid_text(second_clip_id)) return 0;
  try {
    return mutate_timeline(handle, [&](auto& timeline) {
      return timeline.split_clip(clip_id, position_us, second_clip_id,
                                 split_linked != 0);
    }) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

int digitor_timeline_completion_remove_clip(
    DigitorTimelineCompletionHandle* handle,
    const char* clip_id,
    int remove_linked) {
  if (!valid_text(clip_id)) return 0;
  try {
    return mutate_timeline(handle, [&](auto& timeline) {
      return timeline.remove_clip(clip_id, remove_linked != 0);
    }, true) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

int digitor_timeline_completion_project_info(
    const DigitorTimelineCompletionHandle* handle,
    DigitorTimelineProjectInfo* out_info) {
  if (handle == nullptr || out_info == nullptr ||
      out_info->struct_size < sizeof(DigitorTimelineProjectInfo)) return 0;
  try {
    DigitorTimelineProjectInfo info{};
    info.struct_size = sizeof(DigitorTimelineProjectInfo);
    const auto& project = handle->engine.project();
    info.revision = project.revision;
    const digitor::MultitrackTimeline timeline(project.timeline);
    info.duration_us = timeline.duration_us();
    for (const auto& track : project.timeline.tracks) {
      if (track.type == digitor::TimelineTrackType::video) ++info.video_track_count;
      else ++info.audio_track_count;
      info.clip_count += static_cast<uint32_t>(std::min<std::size_t>(
          track.clips.size(), static_cast<std::size_t>(UINT32_MAX - info.clip_count)));
    }
    info.valid = handle->engine.validate() ? 1 : 0;
    *out_info = info;
    return 1;
  } catch (...) {
    return 0;
  }
}

int digitor_timeline_completion_set_track_enabled(
    DigitorTimelineCompletionHandle* handle,
    const char* track_id,
    int enabled) {
  if (handle == nullptr || track_id == nullptr || track_id[0] == '\0') return 0;
  try {
    return handle->engine.set_track_enabled(track_id, enabled != 0) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

int digitor_timeline_completion_track_enabled(
    const DigitorTimelineCompletionHandle* handle,
    const char* track_id,
    int* out_enabled) {
  if (handle == nullptr || track_id == nullptr || track_id[0] == '\0' || out_enabled == nullptr) return 0;
  try {
    const auto& tracks = handle->engine.project().timeline.tracks;
    const auto it = std::find_if(tracks.begin(), tracks.end(),
                                 [&](const auto& track) { return track.id == track_id; });
    if (it == tracks.end()) return 0;
    *out_enabled = handle->engine.track_enabled(track_id) ? 1 : 0;
    return 1;
  } catch (...) {
    return 0;
  }
}

int digitor_timeline_completion_remove_track(
    DigitorTimelineCompletionHandle* handle,
    const char* track_id,
    DigitorTimelineTrackRemovalPolicy policy) {
  if (handle == nullptr || track_id == nullptr || track_id[0] == '\0') return 0;
  if (policy < DIGITOR_TIMELINE_TRACK_REJECT_IF_NOT_EMPTY ||
      policy > DIGITOR_TIMELINE_TRACK_REMOVE_CLIPS_AND_LINKED) {
    return 0;
  }
  try {
    return handle->engine.remove_track(
               track_id, static_cast<digitor::TrackRemovalPolicy>(policy))
               ? 1
               : 0;
  } catch (...) {
    return 0;
  }
}

int digitor_timeline_completion_validate(const DigitorTimelineCompletionHandle* handle) {
  if (handle == nullptr) return 0;
  try {
    return handle->engine.validate() ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

int digitor_timeline_completion_sample(const DigitorTimelineCompletionHandle* handle,
                                       int64_t timeline_us,
                                       DigitorTimelineCompletionSnapshot* out_snapshot) {
  if (handle == nullptr || out_snapshot == nullptr || timeline_us < 0) return 0;
  if (out_snapshot->struct_size < sizeof(DigitorTimelineCompletionSnapshot)) return 0;
  try {
    const auto sample = handle->engine.sample(timeline_us);
    DigitorTimelineCompletionSnapshot snapshot{};
    snapshot.struct_size = sizeof(DigitorTimelineCompletionSnapshot);
    snapshot.revision = sample.revision;
    snapshot.timeline_us = timeline_us;
    snapshot.video_layer_count = static_cast<uint32_t>(std::min<std::size_t>(
        sample.render_plan.video_layers.size(), static_cast<std::size_t>(UINT32_MAX)));
    snapshot.audio_layer_count = static_cast<uint32_t>(std::min<std::size_t>(
        sample.render_plan.audio_layers.size(), static_cast<std::size_t>(UINT32_MAX)));
    snapshot.automation_value_count = static_cast<uint32_t>(std::min<std::size_t>(
        sample.automation_values.size(), static_cast<std::size_t>(UINT32_MAX)));
    snapshot.transition_value_count = static_cast<uint32_t>(std::min<std::size_t>(
        sample.transition_values.size(), static_cast<std::size_t>(UINT32_MAX)));
    snapshot.multicam_group_count = static_cast<uint32_t>(std::min<std::size_t>(
        sample.active_multicam_angles.size(), static_cast<std::size_t>(UINT32_MAX)));
    snapshot.valid = handle->engine.validate() ? 1 : 0;
    *out_snapshot = snapshot;
    return 1;
  } catch (...) {
    return 0;
  }
}

size_t digitor_timeline_completion_serialize_size(const DigitorTimelineCompletionHandle* handle) {
  if (handle == nullptr) return 0U;
  try {
    return handle->engine.serialize().size() + 1U;
  } catch (...) {
    return 0U;
  }
}

int digitor_timeline_completion_serialize(const DigitorTimelineCompletionHandle* handle,
                                          char* output,
                                          size_t output_capacity) {
  if (handle == nullptr || output == nullptr || output_capacity == 0U) return 0;
  try {
    const std::string text = handle->engine.serialize();
    if (output_capacity <= text.size()) return 0;
    std::memcpy(output, text.data(), text.size());
    output[text.size()] = '\0';
    return 1;
  } catch (...) {
    return 0;
  }
}

}  // extern "C"
