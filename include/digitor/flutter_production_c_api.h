#ifndef DIGITOR_FLUTTER_PRODUCTION_C_API_H
#define DIGITOR_FLUTTER_PRODUCTION_C_API_H

#include "digitor/digitor.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIGITOR_FLUTTER_PRODUCTION_HOST_VERSION 2u
#define DIGITOR_FLUTTER_PREVIEW_TARGET_VERSION 1u
#define DIGITOR_FLUTTER_EXPORT_REQUEST_VERSION 1u

typedef struct DigitorFlutterProductionSession DigitorFlutterProductionSession;

typedef enum DigitorFlutterProductionRenderMode {
    DIGITOR_FLUTTER_RENDER_PREVIEW = 0,
    DIGITOR_FLUTTER_RENDER_EXPORT = 1
} DigitorFlutterProductionRenderMode;

typedef struct DigitorFlutterPreviewTarget {
    uint32_t struct_size;
    uint32_t api_version;
    uint64_t native_target_handle;
    uint32_t width;
    uint32_t height;
    int32_t handle_type;
} DigitorFlutterPreviewTarget;

typedef struct DigitorFlutterExportRequest {
    uint32_t struct_size;
    uint32_t api_version;
    const char* utf8_output_path;
    int32_t format;
    int32_t codec;
    int64_t first_frame;
    int64_t last_frame;
    uint32_t width;
    uint32_t height;
} DigitorFlutterExportRequest;

typedef DigitorResult (*DigitorFlutterOpenMediaCallback)(
    void* user_data,
    const char* utf8_path,
    char* diagnostic,
    uint32_t diagnostic_capacity
);

typedef DigitorResult (*DigitorFlutterRenderFrameCallback)(
    void* user_data,
    DigitorFlutterProductionRenderMode mode,
    DigitorNodeGraph* graph,
    uint64_t graph_revision,
    uint64_t parameter_revision,
    int64_t timestamp_us,
    uint32_t width,
    uint32_t height,
    DigitorNativeGpuTextureDescriptor* out_texture,
    char* diagnostic,
    uint32_t diagnostic_capacity
);

typedef DigitorResult (*DigitorFlutterExportMediaCallback)(
    void* user_data,
    DigitorNodeGraph* graph,
    uint64_t graph_revision,
    uint64_t parameter_revision,
    const DigitorFlutterExportRequest* request,
    DigitorExportProgressCallback progress,
    void* progress_user_data,
    char* diagnostic,
    uint32_t diagnostic_capacity
);

typedef DigitorResult (*DigitorFlutterSetPreviewTargetCallback)(
    void* user_data,
    const DigitorFlutterPreviewTarget* target,
    char* diagnostic,
    uint32_t diagnostic_capacity
);

typedef DigitorResult (*DigitorFlutterQueryPreviewCallback)(
    void* user_data,
    DigitorNativePreviewCapabilities* out_capabilities
);

typedef DigitorResult (*DigitorFlutterCancelCallback)(void* user_data);
typedef void (*DigitorFlutterCloseMediaCallback)(void* user_data);
typedef void (*DigitorFlutterReleaseTextureCallback)(
    void* user_data,
    const DigitorNativeGpuTextureDescriptor* texture
);

typedef struct DigitorFlutterProductionHost {
    uint32_t struct_size;
    uint32_t api_version;
    void* user_data;
    uint64_t required_device_identity;
    uint64_t required_context_identity;
    DigitorFlutterOpenMediaCallback open_media;
    DigitorFlutterRenderFrameCallback render_frame;
    DigitorFlutterExportMediaCallback export_media;
    DigitorFlutterQueryPreviewCallback query_preview;
    DigitorFlutterSetPreviewTargetCallback set_preview_target;
    DigitorFlutterCancelCallback cancel;
    DigitorFlutterCloseMediaCallback close_media;
    DigitorFlutterReleaseTextureCallback release_texture;
} DigitorFlutterProductionHost;

/* Native Flutter plugins install exactly one engine-owned production host after
 * their real platform renderer/timeline/encoder bindings are attached. Dart
 * never supplies callback pointers. The host owner must remain alive until it
 * unregisters the same user_data value and all sessions have been destroyed. */
DIGITOR_API DigitorResult digitor_flutter_production_register_host(
    const DigitorFlutterProductionHost* host
);

DIGITOR_API DigitorResult digitor_flutter_production_unregister_host(
    void* expected_user_data
);

DIGITOR_API int32_t digitor_flutter_production_host_registered(void);

/* Creates a production session from the platform host previously installed by
 * the Flutter plugin. This is the entry point used by the high-level editor
 * workspace so application Dart code never handles native callback pointers. */
DIGITOR_API DigitorResult digitor_flutter_production_create_registered(
    const char* utf8_media_path,
    DigitorFlutterProductionSession** out_session
);

DIGITOR_API DigitorResult digitor_flutter_production_create(
    const DigitorFlutterProductionHost* host,
    const char* utf8_media_path,
    DigitorFlutterProductionSession** out_session
);

DIGITOR_API DigitorResult digitor_flutter_production_destroy(
    DigitorFlutterProductionSession* session
);

DIGITOR_API DigitorResult digitor_flutter_production_bind_node_graph(
    DigitorFlutterProductionSession* session,
    DigitorNodeGraph* graph,
    uint64_t graph_revision,
    uint64_t parameter_revision
);

DIGITOR_API DigitorResult digitor_flutter_production_set_preview_target(
    DigitorFlutterProductionSession* session,
    const DigitorFlutterPreviewTarget* target
);

DIGITOR_API DigitorResult digitor_flutter_production_preview(
    DigitorFlutterProductionSession* session,
    int64_t timestamp_us,
    uint32_t width,
    uint32_t height,
    DigitorNativeGpuTextureDescriptor* out_texture
);

DIGITOR_API DigitorResult digitor_flutter_production_preview_consumed(
    DigitorFlutterProductionSession* session,
    uint64_t generation
);

DIGITOR_API DigitorResult digitor_flutter_production_query_preview(
    DigitorFlutterProductionSession* session,
    DigitorNativePreviewCapabilities* out_capabilities
);

DIGITOR_API DigitorResult digitor_flutter_production_export(
    DigitorFlutterProductionSession* session,
    const DigitorFlutterExportRequest* request,
    DigitorExportProgressCallback progress,
    void* progress_user_data
);

DIGITOR_API DigitorResult digitor_flutter_production_cancel(
    DigitorFlutterProductionSession* session
);

DIGITOR_API DigitorResult digitor_flutter_production_get_last_error(
    DigitorFlutterProductionSession* session,
    char* buffer,
    uint32_t* inout_size
);

#ifdef __cplusplus
}
#endif

#endif
