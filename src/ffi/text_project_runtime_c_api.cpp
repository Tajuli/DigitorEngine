#include "digitor/text_project_runtime.hpp"
#include "digitor/text_project_runtime_c.h"

#include <cstring>
#include <exception>
#include <string>

extern "C" {

int digitor_validate_utf8(const char* utf8_text) {
  if (!utf8_text) return DIGITOR_TEXT_PROJECT_INVALID_ARGUMENT;
  try {
    (void)digitor::decode_utf8(utf8_text);
    return DIGITOR_TEXT_PROJECT_OK;
  } catch (...) {
    return DIGITOR_TEXT_PROJECT_ERROR;
  }
}

int digitor_run_progress_task(uint32_t steps, DigitorProgressCallback callback,
                              void* user_data) {
  if (steps == 0) return DIGITOR_TEXT_PROJECT_INVALID_ARGUMENT;
  try {
    const auto result = digitor::run_progress_task(
        steps, [callback, user_data](double progress, const std::string& stage) {
          return callback == nullptr || callback(progress, stage.c_str(), user_data) != 0;
        });
    if (result.cancelled) return DIGITOR_TEXT_PROJECT_CANCELLED;
    return result.completed ? DIGITOR_TEXT_PROJECT_OK : DIGITOR_TEXT_PROJECT_ERROR;
  } catch (...) {
    return DIGITOR_TEXT_PROJECT_ERROR;
  }
}

int digitor_project_roundtrip(const char* serialized, char* output,
                              size_t output_capacity, size_t* required_size) {
  if (!serialized || !required_size) return DIGITOR_TEXT_PROJECT_INVALID_ARGUMENT;
  try {
    const auto project = digitor::deserialize_project(serialized);
    const std::string result = digitor::serialize_project(project);
    *required_size = result.size() + 1;
    if (!output || output_capacity < *required_size)
      return DIGITOR_TEXT_PROJECT_BUFFER_TOO_SMALL;
    std::memcpy(output, result.c_str(), *required_size);
    return DIGITOR_TEXT_PROJECT_OK;
  } catch (...) {
    *required_size = 0;
    return DIGITOR_TEXT_PROJECT_ERROR;
  }
}

}  // extern "C"
