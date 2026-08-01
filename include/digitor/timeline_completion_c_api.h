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

DIGITOR_TIMELINE_API DigitorTimelineCompletionHandle* digitor_timeline_completion_create(void);
DIGITOR_TIMELINE_API void digitor_timeline_completion_destroy(DigitorTimelineCompletionHandle* handle);
DIGITOR_TIMELINE_API int digitor_timeline_completion_load(
    DigitorTimelineCompletionHandle* handle,
    const char* serialized_project,
    size_t serialized_size);
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
