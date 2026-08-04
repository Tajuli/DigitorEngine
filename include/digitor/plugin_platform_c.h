#ifndef DIGITOR_PLUGIN_PLATFORM_C_H
#define DIGITOR_PLUGIN_PLATFORM_C_H

#include "digitor/digitor.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DigitorPluginKindC {
    DIGITOR_PLUGIN_KIND_FILTER = 0,
    DIGITOR_PLUGIN_KIND_EFFECT = 1,
    DIGITOR_PLUGIN_KIND_TRANSITION = 2
} DigitorPluginKindC;

typedef enum DigitorPluginSurfaceC {
    DIGITOR_PLUGIN_SURFACE_PREVIEW = 0,
    DIGITOR_PLUGIN_SURFACE_EXPORT = 1
} DigitorPluginSurfaceC;

typedef struct DigitorPluginFrameC {
    uint64_t native_texture_handle;
    uint32_t width;
    uint32_t height;
    DigitorPixelFormat format;
} DigitorPluginFrameC;

typedef struct DigitorPluginParameterC {
    const char* id;
    double value;
} DigitorPluginParameterC;

typedef struct DigitorPluginRequestC {
    uint32_t struct_size;
    DigitorPluginKindC kind;
    DigitorPluginSurfaceC surface;
    const char* instance_id;
    const char* plugin_id;
    const char* plugin_version;
    const char* project_or_clip_id;
    const DigitorPluginParameterC* parameters;
    size_t parameter_count;
    DigitorPluginFrameC input;
    DigitorPluginFrameC incoming;
    DigitorPluginFrameC output;
    double transition_progress;
} DigitorPluginRequestC;

typedef DigitorResult (*DigitorPluginProcessCallbackC)(
    void* user_data,
    const DigitorPluginRequestC* request,
    char* diagnostic,
    size_t diagnostic_capacity);

typedef struct DigitorPluginPlatformBindingsC {
    uint32_t struct_size;
    void* user_data;
    DigitorPluginProcessCallbackC process_single_input;
    DigitorPluginProcessCallbackC process_transition;
} DigitorPluginPlatformBindingsC;

typedef struct DigitorPluginPlatformC DigitorPluginPlatformC;

DIGITOR_API DigitorResult digitor_plugin_platform_create(
    const DigitorPluginPlatformBindingsC* bindings,
    DigitorPluginPlatformC** out_platform);

DIGITOR_API void digitor_plugin_platform_destroy(
    DigitorPluginPlatformC* platform);

DIGITOR_API DigitorResult digitor_plugin_platform_process(
    DigitorPluginPlatformC* platform,
    const DigitorPluginRequestC* request,
    char* diagnostic,
    size_t diagnostic_capacity);

DIGITOR_API uint32_t digitor_plugin_platform_c_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif
