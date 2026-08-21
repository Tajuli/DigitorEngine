#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIGITOR_ANDROID_FLUTTER_HOST_BRIDGE_API_VERSION 1u
#define DIGITOR_ANDROID_FLUTTER_HOST_BRIDGE_MAGIC UINT64_C(0x4449475052473031)

typedef void (*DigitorAndroidExportProgressBegin)(void* user_data);
typedef void (*DigitorAndroidExportProgressUpdate)(
    void* user_data,
    double fraction,
    int64_t completed,
    int64_t total);
typedef void (*DigitorAndroidExportProgressEnd)(
    void* user_data,
    int32_t result_code);

typedef struct DigitorAndroidFlutterHostBridge {
  uint32_t struct_size;
  uint32_t api_version;
  uint64_t magic;
  void* user_data;
  DigitorAndroidExportProgressBegin export_progress_begin;
  DigitorAndroidExportProgressUpdate export_progress_update;
  DigitorAndroidExportProgressEnd export_progress_end;
} DigitorAndroidFlutterHostBridge;

#ifdef __cplusplus
}
#endif
