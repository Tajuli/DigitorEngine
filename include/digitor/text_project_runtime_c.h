#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*DigitorProgressCallback)(double progress, const char* stage, void* user_data);

enum DigitorTextProjectStatus {
  DIGITOR_TEXT_PROJECT_OK = 0,
  DIGITOR_TEXT_PROJECT_INVALID_ARGUMENT = 1,
  DIGITOR_TEXT_PROJECT_BUFFER_TOO_SMALL = 2,
  DIGITOR_TEXT_PROJECT_CANCELLED = 3,
  DIGITOR_TEXT_PROJECT_ERROR = 4
};

int digitor_validate_utf8(const char* utf8_text);
int digitor_run_progress_task(uint32_t steps, DigitorProgressCallback callback,
                              void* user_data);
int digitor_project_roundtrip(const char* serialized, char* output,
                              size_t output_capacity, size_t* required_size);

#ifdef __cplusplus
}
#endif
