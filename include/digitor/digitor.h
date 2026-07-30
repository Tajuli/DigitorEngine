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
    DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT = 1,
    DIGITOR_PIXEL_FORMAT_RGBA8_UNORM = 2,
    DIGITOR_PIXEL_FORMAT_BGRA8_UNORM = 3,
    DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT = 4
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

typedef enum DigitorFilter {
    DIGITOR_FILTER_NEAREST = 0,
    DIGITOR_FILTER_LINEAR = 1
} DigitorFilter;

typedef enum DigitorAddressMode {
    DIGITOR_ADDRESS_CLAMP_TO_EDGE = 0,
    DIGITOR_ADDRESS_REPEAT = 1,
    DIGITOR_ADDRESS_MIRRORED_REPEAT = 2
} DigitorAddressMode;

typedef struct DigitorSamplerDesc {
    DigitorFilter min_filter;
    DigitorFilter mag_filter;
    DigitorFilter mip_filter;
    DigitorAddressMode address_u;
    DigitorAddressMode address_v;
    DigitorAddressMode address_w;
    uint8_t normalized_coordinates;
} DigitorSamplerDesc;

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
typedef struct DigitorSampler DigitorSampler;
typedef struct DigitorSdkSession DigitorSdkSession;
typedef struct DigitorNodeGraph DigitorNodeGraph;



typedef uint64_t DigitorNodeId;

typedef enum DigitorNodeKind {
    DIGITOR_NODE_INPUT = 0,
    DIGITOR_NODE_SERIAL = 1,
    DIGITOR_NODE_PARALLEL = 2,
    DIGITOR_NODE_MIXER = 3,
    DIGITOR_NODE_OUTPUT = 4
} DigitorNodeKind;

typedef struct DigitorNodePosition { float x; float y; } DigitorNodePosition;
typedef struct DigitorRgb { float r; float g; float b; } DigitorRgb;
typedef struct DigitorPrimaryWheelsControls {
    DigitorRgb lift; float lift_master; uint8_t lift_enabled;
    DigitorRgb gamma; float gamma_master; uint8_t gamma_enabled;
    DigitorRgb gain; float gain_master; uint8_t gain_enabled;
    DigitorRgb offset; float offset_master; uint8_t offset_enabled;
} DigitorPrimaryWheelsControls;
typedef struct DigitorLogWheelControl { DigitorRgb rgb; float master; uint8_t enabled; } DigitorLogWheelControl;
typedef struct DigitorLogWheelsControls {
    DigitorLogWheelControl shadows, midtones, highlights, global;
    float shadow_pivot, highlight_pivot, transition_width;
} DigitorLogWheelsControls;

typedef struct DigitorCurvePoint { float x; float y; } DigitorCurvePoint;
typedef struct DigitorCurveChannel { const DigitorCurvePoint* points; uint32_t point_count; uint8_t enabled; } DigitorCurveChannel;
typedef struct DigitorRgbCurvesControls {
    DigitorCurveChannel master, red, green, blue; uint32_t lut_size;
} DigitorRgbCurvesControls;
typedef struct DigitorQualifierRange { float low; float high; float softness; } DigitorQualifierRange;
typedef struct DigitorHslQualifierControls {
    DigitorQualifierRange hue, saturation, luminance;
    float blur, denoise, clean_black, clean_white; uint8_t invert; uint8_t matte_output;
} DigitorHslQualifierControls;
typedef struct DigitorLutColor { float r,g,b,a; } DigitorLutColor;
typedef enum DigitorLutInterpolation { DIGITOR_LUT_NEAREST=0, DIGITOR_LUT_LINEAR=1, DIGITOR_LUT_TETRAHEDRAL=2 } DigitorLutInterpolation;
typedef struct DigitorLut1DControls { const DigitorLutColor* values; uint32_t value_count; DigitorLutInterpolation interpolation; } DigitorLut1DControls;
typedef struct DigitorLut3DControls { uint32_t size; const DigitorLutColor* values; uint64_t value_count; DigitorLutInterpolation interpolation; } DigitorLut3DControls;

typedef enum DigitorNodeEffectType {
    DIGITOR_NODE_EFFECT_BLUR = 0, DIGITOR_NODE_EFFECT_SHARPEN = 1,
    DIGITOR_NODE_EFFECT_GLOW = 2, DIGITOR_NODE_EFFECT_LENS_DISTORTION = 3,
    DIGITOR_NODE_EFFECT_NOISE = 4, DIGITOR_NODE_EFFECT_FILM_GRAIN = 5,
    DIGITOR_NODE_EFFECT_CHROMATIC_ABERRATION = 6, DIGITOR_NODE_EFFECT_VIGNETTE = 7,
    DIGITOR_NODE_EFFECT_MOTION_BLUR = 8
} DigitorNodeEffectType;
typedef struct DigitorNodeEffectSettings { DigitorNodeEffectType type; float amount; float radius; float angle; uint64_t seed; } DigitorNodeEffectSettings;
typedef enum DigitorPowerWindowShape { DIGITOR_WINDOW_RECTANGLE=0, DIGITOR_WINDOW_ELLIPSE=1, DIGITOR_WINDOW_LINEAR_GRADIENT=2 } DigitorPowerWindowShape;
typedef struct DigitorPowerWindowSettings {
    DigitorPowerWindowShape shape; float center_x, center_y, width, height, rotation, feather, opacity; uint8_t invert;
} DigitorPowerWindowSettings;

typedef struct DigitorColorControls { float exposure; float contrast; float saturation; } DigitorColorControls;
typedef struct DigitorNativeTexture { const void* pixels; uint32_t width; uint32_t height; uint32_t row_bytes; uint64_t generation; } DigitorNativeTexture;
typedef void (*DigitorAsyncCallback)(DigitorResult result, void* user_data);
typedef void (*DigitorExportProgressCallback)(double fraction, int64_t completed, int64_t total, void* user_data);

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

/* Upload and staging buffers are host-visible. A buffer may only be mapped once;
 * offset + size must be within the allocation. Passing size == 0 maps the
 * remainder of the buffer. The returned pointer remains valid until unmap. */
DIGITOR_API DigitorResult digitor_map_buffer(
    DigitorBuffer* buffer,
    uint64_t offset,
    uint64_t size,
    void** out_data
);

DIGITOR_API DigitorResult digitor_unmap_buffer(DigitorBuffer* buffer);

DIGITOR_API DigitorResult digitor_create_sampler(
    DigitorRenderContext* context,
    const DigitorSamplerDesc* desc,
    DigitorSampler** out_sampler
);

DIGITOR_API DigitorResult digitor_destroy_sampler(DigitorSampler* sampler);

/* Flutter-safe asynchronous SDK. Callbacks run on a worker thread; clients
 * marshal them to their UI isolate. The texture remains valid until the next
 * preview request or session destruction. */
DIGITOR_API DigitorResult digitor_sdk_create(DigitorSdkSession** out_session);
DIGITOR_API DigitorResult digitor_sdk_destroy(DigitorSdkSession* session);
DIGITOR_API DigitorResult digitor_sdk_set_color(DigitorSdkSession* session, DigitorColorControls controls);
DIGITOR_API DigitorResult digitor_sdk_preview_async(DigitorSdkSession* session, int64_t frame, uint32_t width, uint32_t height, DigitorAsyncCallback callback, void* user_data);
DIGITOR_API DigitorResult digitor_sdk_seek_async(DigitorSdkSession* session, int64_t frame, DigitorAsyncCallback callback, void* user_data);
DIGITOR_API DigitorResult digitor_sdk_get_native_texture(DigitorSdkSession* session, DigitorNativeTexture* out_texture);
DIGITOR_API DigitorResult digitor_sdk_export_async(DigitorSdkSession* session, const char* path, int32_t format, int32_t codec, int64_t first, int64_t last, uint32_t width, uint32_t height, DigitorExportProgressCallback progress, DigitorAsyncCallback completion, void* user_data);
DIGITOR_API DigitorResult digitor_sdk_cancel(DigitorSdkSession* session);


/* Production node graph C ABI. IDs are stable for the graph lifetime. Input,
 * output and mixer nodes are not selectable. JSON is deterministic and can be
 * requested with buffer == NULL to obtain the required byte count. */
DIGITOR_API DigitorResult digitor_node_graph_create(DigitorNodeGraph** out_graph);
DIGITOR_API DigitorResult digitor_node_graph_destroy(DigitorNodeGraph* graph);
DIGITOR_API DigitorResult digitor_node_graph_get_endpoints(DigitorNodeGraph* graph, DigitorNodeId* out_input, DigitorNodeId* out_output);
DIGITOR_API DigitorResult digitor_node_graph_select(DigitorNodeGraph* graph, DigitorNodeId node);
DIGITOR_API DigitorResult digitor_node_graph_get_selected(DigitorNodeGraph* graph, DigitorNodeId* out_node);
DIGITOR_API DigitorResult digitor_node_graph_add_serial_after(DigitorNodeGraph* graph, DigitorNodeId after, const char* name, DigitorNodeId* out_node);
DIGITOR_API DigitorResult digitor_node_graph_add_parallel_after(DigitorNodeGraph* graph, DigitorNodeId after, const char* first_name, const char* second_name, DigitorNodeId* out_first, DigitorNodeId* out_second);
DIGITOR_API DigitorResult digitor_node_graph_convert_to_parallel(DigitorNodeGraph* graph, DigitorNodeId existing, const char* branch_name, DigitorNodeId* out_branch);
DIGITOR_API DigitorResult digitor_node_graph_remove(DigitorNodeGraph* graph, DigitorNodeId node);
DIGITOR_API DigitorResult digitor_node_graph_connect(DigitorNodeGraph* graph, DigitorNodeId source, DigitorNodeId destination);
DIGITOR_API DigitorResult digitor_node_graph_disconnect(DigitorNodeGraph* graph, DigitorNodeId source, DigitorNodeId destination);
DIGITOR_API DigitorResult digitor_node_graph_set_position(DigitorNodeGraph* graph, DigitorNodeId node, DigitorNodePosition position);
DIGITOR_API DigitorResult digitor_node_graph_set_enabled(DigitorNodeGraph* graph, DigitorNodeId node, uint8_t enabled);
DIGITOR_API DigitorResult digitor_node_graph_set_bypassed(DigitorNodeGraph* graph, DigitorNodeId node, uint8_t bypassed);
DIGITOR_API DigitorResult digitor_node_graph_clear_operations(DigitorNodeGraph* graph, DigitorNodeId node);
DIGITOR_API DigitorResult digitor_node_graph_add_primary_wheels(DigitorNodeGraph* graph, const DigitorPrimaryWheelsControls* controls);
DIGITOR_API DigitorResult digitor_node_graph_add_log_wheels(DigitorNodeGraph* graph, const DigitorLogWheelsControls* controls);
DIGITOR_API DigitorResult digitor_node_graph_add_rgb_curves(DigitorNodeGraph* graph, const DigitorRgbCurvesControls* controls);
DIGITOR_API DigitorResult digitor_node_graph_add_hsl_qualifier(DigitorNodeGraph* graph, const DigitorHslQualifierControls* controls);
DIGITOR_API DigitorResult digitor_node_graph_add_lut1d(DigitorNodeGraph* graph, const DigitorLut1DControls* controls);
DIGITOR_API DigitorResult digitor_node_graph_add_lut3d(DigitorNodeGraph* graph, const DigitorLut3DControls* controls);
DIGITOR_API DigitorResult digitor_node_graph_add_effect(DigitorNodeGraph* graph, const DigitorNodeEffectSettings* settings);
DIGITOR_API DigitorResult digitor_node_graph_add_power_window(DigitorNodeGraph* graph, const DigitorPowerWindowSettings* settings);
DIGITOR_API DigitorResult digitor_node_graph_recipe_identity(DigitorNodeGraph* graph, char* buffer, uint64_t buffer_size, uint64_t* out_required);
DIGITOR_API DigitorResult digitor_node_graph_to_json(DigitorNodeGraph* graph, char* buffer, uint64_t buffer_size, uint64_t* out_required);

#ifdef __cplusplus
}
#endif

#endif
