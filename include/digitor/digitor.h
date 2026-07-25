#ifndef DIGITOR_DIGITOR_H
#define DIGITOR_DIGITOR_H

#include <stdint.h>

#if defined(DIGITOR_STATIC)
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
    DIGITOR_RESULT_INTERNAL_ERROR = 100
} DigitorResult;

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

#ifdef __cplusplus
}
#endif

#endif
