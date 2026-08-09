#pragma once

#include <stdint.h>

#if defined(DIGITOR_ENGINE_STATIC)
  #define DIGITOR_MEDIA_API
#elif defined(_WIN32)
  #if defined(DIGITOR_ENGINE_BUILD)
    #define DIGITOR_MEDIA_API __declspec(dllexport)
  #else
    #define DIGITOR_MEDIA_API __declspec(dllimport)
  #endif
#else
  #define DIGITOR_MEDIA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DIGITOR_PRODUCTION_MEDIA_OPTIONS_VERSION 1u
#define DIGITOR_PRODUCTION_DECODER_INFO_VERSION 1u
#define DIGITOR_PRODUCTION_FRAME_INFO_VERSION 1u
#define DIGITOR_PRODUCTION_NATIVE_SURFACE_VERSION 1u

typedef struct DigitorProductionMediaSource DigitorProductionMediaSource;

typedef enum DigitorProductionHardwareDecode {
  DIGITOR_PRODUCTION_DECODE_AUTO = 0,
  DIGITOR_PRODUCTION_DECODE_CPU = 1,
  DIGITOR_PRODUCTION_DECODE_DXVA = 2,
  DIGITOR_PRODUCTION_DECODE_VIDEOTOOLBOX = 3,
  DIGITOR_PRODUCTION_DECODE_MEDIACODEC = 4
} DigitorProductionHardwareDecode;

typedef enum DigitorProductionPixelFormat {
  DIGITOR_PRODUCTION_PIXEL_RGBA32F = 0,
  DIGITOR_PRODUCTION_PIXEL_RGBA8 = 1,
  DIGITOR_PRODUCTION_PIXEL_BGRA8 = 2,
  DIGITOR_PRODUCTION_PIXEL_NV12 = 3,
  DIGITOR_PRODUCTION_PIXEL_YUV420P = 4,
  DIGITOR_PRODUCTION_PIXEL_P010 = 5,
  DIGITOR_PRODUCTION_PIXEL_YUV420P10 = 6
} DigitorProductionPixelFormat;

typedef struct DigitorProductionMediaOptions {
  uint32_t struct_size;
  uint32_t api_version;
  uint32_t hardware_decode;
  uint8_t allow_cpu_fallback;
  uint8_t require_zero_copy;
  uint16_t reserved;
  uint32_t cache_capacity;
} DigitorProductionMediaOptions;

typedef struct DigitorProductionDecoderInfo {
  uint32_t struct_size;
  uint32_t api_version;
  uint32_t selected_hardware_decode;
  uint8_t hardware_accelerated;
  uint8_t native_surface_output;
  uint16_t reserved;
  uint32_t native_handle_type;
  char implementation[128];
} DigitorProductionDecoderInfo;

typedef struct DigitorProductionDecodedFrameInfo {
  uint32_t struct_size;
  uint32_t api_version;
  int64_t frame_number;
  int64_t pts_us;
  int64_t duration_us;
  uint32_t width;
  uint32_t height;
  uint32_t pixel_format;
  uint8_t gpu_resident;
  uint8_t cpu_resident;
  uint16_t reserved;
} DigitorProductionDecodedFrameInfo;

/* Pure borrowed descriptor for the most recently decoded native GPU surface.
 * The underlying decoder surface remains retained by DigitorProductionMediaSource
 * until the next successful decode, seek, or close. Callers must not destroy,
 * release, map, or reinterpret the native handle as CPU memory. */
typedef struct DigitorProductionNativeSurfaceDescriptor {
  uint32_t struct_size;
  uint32_t api_version;
  uint32_t platform;
  uint32_t handle_type;
  uint32_t pixel_format;
  uint32_t width;
  uint32_t height;
  uint32_t plane_count;
  uint32_t array_slice;
  uint64_t native_handle;
  uint64_t native_device;
  uint64_t allocation_size;
  int64_t timestamp_us;
  uint32_t acquire_sync_type;
  uint32_t reserved0;
  uint64_t acquire_sync_handle;
  uint64_t acquire_sync_value;
  int32_t color_primaries;
  int32_t transfer_function;
  int32_t matrix_coefficients;
  uint8_t full_range;
  uint8_t chroma_location;
  uint16_t reserved1;
} DigitorProductionNativeSurfaceDescriptor;

/* Returns DigitorResult numeric values declared by digitor.h. */
DIGITOR_MEDIA_API int32_t digitor_production_media_open(
    const char* path,
    const DigitorProductionMediaOptions* options,
    DigitorProductionMediaSource** out_source);
DIGITOR_MEDIA_API int32_t digitor_production_media_get_info(
    const DigitorProductionMediaSource* source,
    DigitorProductionDecoderInfo* out_info);
DIGITOR_MEDIA_API int32_t digitor_production_media_get_duration_us(
    const DigitorProductionMediaSource* source,
    int64_t* out_duration_us);
DIGITOR_MEDIA_API int32_t digitor_production_media_seek(
    DigitorProductionMediaSource* source,
    int64_t pts_us);
DIGITOR_MEDIA_API int32_t digitor_production_media_decode(
    DigitorProductionMediaSource* source,
    int64_t frame_number,
    DigitorProductionDecodedFrameInfo* out_frame);
DIGITOR_MEDIA_API int32_t digitor_production_media_get_native_surface(
    const DigitorProductionMediaSource* source,
    DigitorProductionNativeSurfaceDescriptor* out_surface);
DIGITOR_MEDIA_API void digitor_production_media_close(
    DigitorProductionMediaSource* source);
DIGITOR_MEDIA_API uint8_t digitor_production_media_ffmpeg_available(void);

#ifdef __cplusplus
}
#endif