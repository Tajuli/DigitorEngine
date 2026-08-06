#ifndef DIGITOR_GPU_IMAGE_SESSION_C_API_H
#define DIGITOR_GPU_IMAGE_SESSION_C_API_H

#include "digitor/digitor.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIGITOR_GPU_IMAGE_SESSION_HOST_VERSION 2u
#define DIGITOR_IMAGE_EXPORT_OPTIONS_VERSION 1u

typedef struct DigitorGpuImageSession DigitorGpuImageSession;

typedef enum DigitorGpuImageRenderMode {
    DIGITOR_GPU_IMAGE_RENDER_PREVIEW = 0,
    DIGITOR_GPU_IMAGE_RENDER_EXPORT = 1
} DigitorGpuImageRenderMode;

typedef enum DigitorImageExportFormat {
    DIGITOR_IMAGE_EXPORT_JPEG = 0,
    DIGITOR_IMAGE_EXPORT_PNG = 1,
    DIGITOR_IMAGE_EXPORT_WEBP = 2
} DigitorImageExportFormat;

typedef struct DigitorImageExportOptions {
    uint32_t struct_size;
    uint32_t api_version;
    DigitorImageExportFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t quality;
    uint8_t preserve_alpha;
    uint8_t overwrite_existing;
} DigitorImageExportOptions;

typedef DigitorResult (*DigitorGpuImageOpenCallback)(
    void* user_data,
    const char* utf8_path,
    DigitorNativeGpuTextureDescriptor* out_source,
    char* diagnostic,
    uint32_t diagnostic_capacity
);

/* node_graph is the same production DigitorNodeGraph used by video clips.
 * Implementations execute that graph through the existing video node/color/
 * effect GPU pipeline for both preview and export. No image-only filter or
 * effect implementation is permitted. */
typedef DigitorResult (*DigitorGpuImageProcessCallback)(
    void* user_data,
    DigitorGpuImageRenderMode mode,
    const DigitorNativeGpuTextureDescriptor* source,
    DigitorNodeGraph* node_graph,
    uint32_t width,
    uint32_t height,
    int64_t timestamp_us,
    uint64_t graph_revision,
    uint64_t parameter_revision,
    DigitorNativeGpuTextureDescriptor* out_processed,
    char* diagnostic,
    uint32_t diagnostic_capacity
);

typedef DigitorResult (*DigitorGpuImageExportCallback)(
    void* user_data,
    const DigitorNativeGpuTextureDescriptor* processed,
    const char* utf8_output_path,
    const DigitorImageExportOptions* options,
    char* diagnostic,
    uint32_t diagnostic_capacity
);

typedef void (*DigitorGpuImageReleaseTextureCallback)(
    void* user_data,
    const DigitorNativeGpuTextureDescriptor* texture
);

typedef struct DigitorGpuImageSessionHost {
    uint32_t struct_size;
    uint32_t api_version;
    void* user_data;
    uint64_t required_device_identity;
    uint64_t required_context_identity;
    DigitorGpuImageOpenCallback open_image;
    DigitorGpuImageProcessCallback process_image;
    DigitorGpuImageExportCallback export_image;
    DigitorGpuImageReleaseTextureCallback release_texture;
} DigitorGpuImageSessionHost;

DIGITOR_API DigitorResult digitor_gpu_image_session_create(
    const DigitorGpuImageSessionHost* host,
    const char* utf8_path,
    DigitorGpuImageSession** out_session
);

DIGITOR_API DigitorResult digitor_gpu_image_session_destroy(
    DigitorGpuImageSession* session
);

/* The graph remains caller-owned and must outlive the session binding. */
DIGITOR_API DigitorResult digitor_gpu_image_session_bind_node_graph(
    DigitorGpuImageSession* session,
    DigitorNodeGraph* graph,
    uint64_t graph_revision
);

DIGITOR_API DigitorResult digitor_gpu_image_session_clear_node_graph(
    DigitorGpuImageSession* session,
    uint64_t graph_revision
);

DIGITOR_API DigitorResult digitor_gpu_image_session_get_node_graph(
    DigitorGpuImageSession* session,
    DigitorNodeGraph** out_graph,
    uint64_t* out_graph_revision
);

DIGITOR_API DigitorResult digitor_gpu_image_session_set_graph_revision(
    DigitorGpuImageSession* session,
    uint64_t revision
);

DIGITOR_API DigitorResult digitor_gpu_image_session_set_parameter_revision(
    DigitorGpuImageSession* session,
    uint64_t revision
);

DIGITOR_API DigitorResult digitor_gpu_image_session_render(
    DigitorGpuImageSession* session,
    DigitorGpuImageRenderMode mode,
    uint32_t width,
    uint32_t height,
    int64_t timestamp_us,
    DigitorNativeGpuTextureDescriptor* out_texture
);

DIGITOR_API DigitorResult digitor_gpu_image_session_export(
    DigitorGpuImageSession* session,
    const char* utf8_output_path,
    const DigitorImageExportOptions* options
);

DIGITOR_API DigitorResult digitor_gpu_image_session_get_last_error(
    DigitorGpuImageSession* session,
    char* buffer,
    uint32_t* inout_size
);

#ifdef __cplusplus
}
#endif

#endif
