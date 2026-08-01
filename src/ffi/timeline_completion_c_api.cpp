#include "digitor/timeline_completion_c_api.h"

#include "digitor/timeline_completion.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

struct DigitorTimelineCompletionHandle {
  digitor::TimelineCompletionEngine engine;
};

extern "C" {

DigitorTimelineCompletionHandle* digitor_timeline_completion_create(void) {
  try {
    return new DigitorTimelineCompletionHandle{};
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
    return 1;
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
