#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && !defined(DIGITOR_ENGINE_STATIC)
#if defined(DIGITOR_ENGINE_BUILD)
#define DIGITOR_TIMELINE_API __declspec(dllexport)
#else
#define DIGITOR_TIMELINE_API __declspec(dllimport)
#endif
#else
#define DIGITOR_TIMELINE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DigitorTimelineCompletionHandle DigitorTimelineCompletionHandle;

typedef enum DigitorTimelineTrackRemovalPolicy {
  DIGITOR_TIMELINE_TRACK_REJECT_IF_NOT_EMPTY = 0,
  DIGITOR_TIMELINE_TRACK_REMOVE_CLIPS = 1,
  DIGITOR_TIMELINE_TRACK_REMOVE_CLIPS_AND_LINKED = 2
} DigitorTimelineTrackRemovalPolicy;

typedef enum DigitorTimelineTrackKind {
  DIGITOR_TIMELINE_TRACK_VIDEO = 0,
  DIGITOR_TIMELINE_TRACK_AUDIO = 1
} DigitorTimelineTrackKind;

typedef enum DigitorTimelineClipKind {
  DIGITOR_TIMELINE_CLIP_VIDEO = 0,
  DIGITOR_TIMELINE_CLIP_IMAGE = 1,
  DIGITOR_TIMELINE_CLIP_OVERLAY = 2,
  DIGITOR_TIMELINE_CLIP_TEXT = 3,
  DIGITOR_TIMELINE_CLIP_AUDIO = 4
} DigitorTimelineClipKind;

typedef struct DigitorTimelineCompletionSnapshot {
  uint32_t struct_size;
  uint64_t revision;
  int64_t timeline_us;
  uint32_t video_layer_count;
  uint32_t audio_layer_count;
  uint32_t automation_value_count;
  uint32_t transition_value_count;
  uint32_t multicam_group_count;
  int valid;
} DigitorTimelineCompletionSnapshot;

typedef struct DigitorTimelineProjectInfo {
  uint32_t struct_size;
  uint64_t revision;
  int64_t duration_us;
  uint32_t video_track_count;
  uint32_t audio_track_count;
  uint32_t clip_count;
  int valid;
} DigitorTimelineProjectInfo;

DIGITOR_TIMELINE_API DigitorTimelineCompletionHandle* digitor_timeline_completion_create(void);
DIGITOR_TIMELINE_API void digitor_timeline_completion_destroy(DigitorTimelineCompletionHandle* handle);
DIGITOR_TIMELINE_API int digitor_timeline_completion_load(
    DigitorTimelineCompletionHandle* handle,
    const char* serialized_project,
    size_t serialized_size);

/* Mutations below are authoritative native timeline edits. They reuse the
 * MultitrackTimeline validation/split/delete implementation and bump the
 * completion-project revision only when a mutation succeeds. Source paths stay
 * in the media-source registry; clips carry source_media_group_id identity. */
DIGITOR_TIMELINE_API int digitor_timeline_completion_add_track(
    DigitorTimelineCompletionHandle* handle,
    const char* track_id,
    const char* track_name,
    DigitorTimelineTrackKind kind);
DIGITOR_TIMELINE_API int digitor_timeline_completion_add_clip(
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
    int embedded_audio);
DIGITOR_TIMELINE_API int digitor_timeline_completion_split_clip(
    DigitorTimelineCompletionHandle* handle,
    const char* clip_id,
    int64_t position_us,
    const char* second_clip_id,
    int split_linked);
DIGITOR_TIMELINE_API int digitor_timeline_completion_remove_clip(
    DigitorTimelineCompletionHandle* handle,
    const char* clip_id,
    int remove_linked);
DIGITOR_TIMELINE_API int digitor_timeline_completion_project_info(
    const DigitorTimelineCompletionHandle* handle,
    DigitorTimelineProjectInfo* out_info);

DIGITOR_TIMELINE_API int digitor_timeline_completion_set_track_enabled(
    DigitorTimelineCompletionHandle* handle,
    const char* track_id,
    int enabled);
DIGITOR_TIMELINE_API int digitor_timeline_completion_track_enabled(
    const DigitorTimelineCompletionHandle* handle,
    const char* track_id,
    int* out_enabled);
DIGITOR_TIMELINE_API int digitor_timeline_completion_remove_track(
    DigitorTimelineCompletionHandle* handle,
    const char* track_id,
    DigitorTimelineTrackRemovalPolicy policy);
DIGITOR_TIMELINE_API int digitor_timeline_completion_validate(
    const DigitorTimelineCompletionHandle* handle);
DIGITOR_TIMELINE_API int digitor_timeline_completion_sample(
    const DigitorTimelineCompletionHandle* handle,
    int64_t timeline_us,
    DigitorTimelineCompletionSnapshot* out_snapshot);
DIGITOR_TIMELINE_API size_t digitor_timeline_completion_serialize_size(
    const DigitorTimelineCompletionHandle* handle);
DIGITOR_TIMELINE_API int digitor_timeline_completion_serialize(
    const DigitorTimelineCompletionHandle* handle,
    char* output,
    size_t output_capacity);

#ifdef __cplusplus
}
#endif
