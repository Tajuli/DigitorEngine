#ifndef DIGITOR_SDK_WORKER_C_API_H
#define DIGITOR_SDK_WORKER_C_API_H

#include <stdint.h>

#if defined(_WIN32) && !defined(DIGITOR_ENGINE_STATIC)
#  if defined(DIGITOR_ENGINE_BUILD)
#    define DIGITOR_WORKER_API __declspec(dllexport)
#  else
#    define DIGITOR_WORKER_API __declspec(dllimport)
#  endif
#else
#  define DIGITOR_WORKER_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DigitorSdkWorkerHandle DigitorSdkWorkerHandle;

typedef enum DigitorSdkWorkerKind {
  DIGITOR_SDK_WORKER_PREVIEW = 0,
  DIGITOR_SDK_WORKER_EXPORT = 1
} DigitorSdkWorkerKind;

typedef enum DigitorSdkWorkerCompletion {
  DIGITOR_SDK_WORKER_COMPLETED = 0,
  DIGITOR_SDK_WORKER_CANCELLED = 1,
  DIGITOR_SDK_WORKER_FAILED = 2
} DigitorSdkWorkerCompletion;

typedef void (*DigitorSdkWorkerProgressCallback)(
    void* user_data, uint64_t completed_units, uint64_t total_units);
typedef void (*DigitorSdkWorkerCompletionCallback)(
    void* user_data, DigitorSdkWorkerCompletion completion);

DIGITOR_WORKER_API DigitorSdkWorkerHandle* digitor_sdk_worker_create(
    DigitorSdkWorkerKind kind,
    uint64_t total_units,
    DigitorSdkWorkerProgressCallback progress_callback,
    DigitorSdkWorkerCompletionCallback completion_callback,
    void* user_data);

DIGITOR_WORKER_API int32_t digitor_sdk_worker_start(
    DigitorSdkWorkerHandle* handle);
DIGITOR_WORKER_API int32_t digitor_sdk_worker_cancel(
    DigitorSdkWorkerHandle* handle);
DIGITOR_WORKER_API void digitor_sdk_worker_destroy(
    DigitorSdkWorkerHandle* handle);

#ifdef __cplusplus
}
#endif

#endif
