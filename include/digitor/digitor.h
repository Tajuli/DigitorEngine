#ifndef DIGITOR_DIGITOR_H
#define DIGITOR_DIGITOR_H

#include <stdint.h>

#if defined(DIGITOR_ENGINE_STATIC)
  #define DIGITOR_API
#elif defined(_WIN32)
  #if defined(DIGITOR_ENGINE_BUILD)
    #define DIGITOR_API __declspec(dllexport)
  #else
    #define DIGITOR_API __declspec(dllimport)
  #endif
#else
  #define DIGITOR_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DigitorResult {
    DIGITOR_RESULT_OK = 0,
    DIGITOR_RESULT_INVALID_ARGUMENT = 1,
    DIGITOR_RESULT_NOT_INITIALIZED = 2,
    DIGITOR_RESULT_ALREADY_INITIALIZED = 3,
    DIGITOR_RESULT_BACKEND_UNAVAILABLE = 4,
    DIGITOR_RESULT_UNSUPPORTED = 5,
    DIGITOR_RESULT_RESOURCE_IN_USE = 6,
    DIGITOR_RESULT_OUT_OF_MEMORY = 7,
    DIGITOR_RESULT_INTERNAL_ERROR = 100
} DigitorResult;

typedef enum DigitorPixelFormat {
    DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT = 1
} DigitorPixelFormat;

typedef enum DigitorTextureUsage {
    DIGITOR_TEXTURE_USAGE_SAMPLED = 1u << 0,
    DIGITOR_TEXTURE_USAGE_STORAGE = 1u << 1,
    DIGITOR_TEXTURE_USAGE_RENDER_TARGET = 1u << 2,
    DIGITOR_TEXTURE_USAGE_TRANSFER_SOURCE = 1u << 3,
    DIGITOR_TEXTURE_USAGE_TRANSFER_DESTINATION = 1u << 4
} DigitorTextureUsage;

typedef struct DigitorTextureDesc {
    uint32_t width;
    uint32_t height;
    DigitorPixelFormat format;
    uint32_t usage;
} DigitorTextureDesc;

typedef enum DigitorBufferUsage {
    DIGITOR_BUFFER_USAGE_UNIFORM = 1u << 0,
    DIGITOR_BUFFER_USAGE_STORAGE = 1u << 1,
    DIGITOR_BUFFER_USAGE_UPLOAD = 1u << 2,
    DIGITOR_BUFFER_USAGE_STAGING = 1u << 3
} DigitorBufferUsage;

typedef struct DigitorBufferDesc {
    uint64_t size;
    uint32_t usage;
} DigitorBufferDesc;

typedef enum DigitorRendererBackend {
    DIGITOR_RENDERER_AUTO = 0,
    DIGITOR_RENDERER_VULKAN = 1,
    DIGITOR_RENDERER_METAL = 2,
    DIGITOR_RENDERER_D3D12 = 3,
    DIGITOR_RENDERER_OPENGL_ES = 4,
    DIGITOR_RENDERER_CPU = 100
} DigitorRendererBackend;

typedef struct DigitorEngineConfig {
    DigitorRendererBackend preferred_backend;
    uint8_t enable_validation;
    uint8_t allow_cpu_fallback;
} DigitorEngineConfig;

typedef struct DigitorRendererInfo {
    DigitorRendererBackend backend;
    char backend_name[64];
    char device_name[128];
    uint8_t is_gpu;
    uint8_t supports_compute;
    uint8_t supports_fp16;
    uint8_t supports_fp32;
} DigitorRendererInfo;

typedef struct DigitorRenderContext DigitorRenderContext;
typedef struct DigitorTexture DigitorTexture;
typedef struct DigitorBuffer DigitorBuffer;

DIGITOR_API const char* digitor_get_version(void);

DIGITOR_API DigitorResult digitor_initialize(
    const DigitorEngineConfig* config
);

DIGITOR_API DigitorResult digitor_shutdown(void);

DIGITOR_API DigitorResult digitor_get_renderer_info(
    DigitorRendererInfo* out_info
);

DIGITOR_API DigitorResult digitor_create_render_context(
    DigitorRenderContext** out_context
);

DIGITOR_API DigitorResult digitor_destroy_render_context(
    DigitorRenderContext* context
);

/* Resource handles are owned by their context. Destroy all resources before it. */
DIGITOR_API DigitorResult digitor_create_texture(
    DigitorRenderContext* context,
    const DigitorTextureDesc* desc,
    DigitorTexture** out_texture
);

DIGITOR_API DigitorResult digitor_destroy_texture(DigitorTexture* texture);

DIGITOR_API DigitorResult digitor_create_buffer(
    DigitorRenderContext* context,
    const DigitorBufferDesc* desc,
    DigitorBuffer** out_buffer
);

DIGITOR_API DigitorResult digitor_destroy_buffer(DigitorBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif
