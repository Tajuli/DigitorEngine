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
#define DIGITOR_FLUTTER_EXPORT_REQUEST_V2_VERSION 2u
#define DIGITOR_FLUTTER_PRODUCTION_HOST_V2_VERSION 1u

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


/* V2 is a strict superset used by the lazy export path. The request freezes
 * every revision and render property needed to prove that export matches the
 * editor state that the user approved. V1 remains binary/source compatible. */
typedef struct DigitorFlutterExportRequestV2 {
    uint32_t struct_size;
    uint32_t api_version;
    const char* utf8_output_path;
    int32_t format;
    int32_t codec;
    int64_t first_frame;
    int64_t last_frame;
    uint32_t width;
    uint32_t height;
    uint64_t snapshot_identity;
    uint64_t timeline_revision;
    uint64_t render_revision;
    uint64_t node_graph_revision;
    uint64_t color_pipeline_revision;
    uint64_t audio_revision;
    int32_t working_format;
    uint32_t alpha_policy;
    int32_t fps_num;
    int32_t fps_den;
    int64_t duration_us;
    int64_t video_bitrate;
    uint8_t variable_frame_rate;
    uint8_t hdr;
    uint8_t ten_bit;
    uint8_t reserved0;
    const char* utf8_color_metadata;
    const char* utf8_graph_recipe_identity;
} DigitorFlutterExportRequestV2;

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


typedef DigitorResult (*DigitorFlutterExportMediaV2Callback)(
    void* user_data,
    DigitorNodeGraph* graph,
    uint64_t graph_revision,
    uint64_t parameter_revision,
    const DigitorFlutterExportRequestV2* request,
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


/* Separate superset host contract. The base host layout/version is unchanged;
 * V2 adds only the frozen-snapshot export callback. */
typedef struct DigitorFlutterProductionHostV2 {
    uint32_t struct_size;
    uint32_t api_version;
    DigitorFlutterProductionHost base;
    DigitorFlutterExportMediaV2Callback export_media_v2;
} DigitorFlutterProductionHostV2;

/* Native Flutter plugins install exactly one engine-owned production host after
 * their real platform renderer/timeline/encoder bindings are attached. Dart
 * never supplies callback pointers. The host owner must remain alive until it
 * unregisters the same user_data value and all sessions have been destroyed. */
DIGITOR_API DigitorResult digitor_flutter_production_register_host(
    const DigitorFlutterProductionHost* host
);

DIGITOR_API DigitorResult digitor_flutter_production_register_host_v2(
    const DigitorFlutterProductionHostV2* host
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

DIGITOR_API DigitorResult digitor_flutter_production_create_v2(
    const DigitorFlutterProductionHostV2* host,
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

DIGITOR_API DigitorResult digitor_flutter_production_export_v2(
    DigitorFlutterProductionSession* session,
    const DigitorFlutterExportRequestV2* request,
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
