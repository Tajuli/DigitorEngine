#include <digitor/digitor.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* Compile-time coverage of public fields without assuming platform padding. */
_Static_assert(sizeof(((DigitorTextureDesc*)0)->width) == sizeof(uint32_t),
               "texture width must be uint32_t-sized");
_Static_assert(sizeof(((DigitorTextureDesc*)0)->height) == sizeof(uint32_t),
               "texture height must be uint32_t-sized");
_Static_assert(sizeof(DigitorTextureDesc) >=
                   sizeof(uint32_t) * 3u + sizeof(DigitorPixelFormat),
               "texture descriptor is too small for its public fields");
_Static_assert(sizeof(((DigitorBufferDesc*)0)->size) == sizeof(uint64_t),
               "buffer size must be uint64_t-sized");
_Static_assert(sizeof(DigitorBufferDesc) >= sizeof(uint64_t) + sizeof(uint32_t),
               "buffer descriptor is too small for its public fields");
_Static_assert(sizeof(((DigitorRendererInfo*)0)->backend_name) == 64u,
               "backend_name public array size changed");
_Static_assert(sizeof(((DigitorRendererInfo*)0)->device_name) == 128u,
               "device_name public array size changed");

int main(void) {
    DigitorTextureDesc texture = {
        7u, 5u, DIGITOR_PIXEL_FORMAT_RGBA8_UNORM,
        DIGITOR_TEXTURE_USAGE_SAMPLED | DIGITOR_TEXTURE_USAGE_TRANSFER_DESTINATION
    };
    DigitorBufferDesc buffer = {
        UINT64_C(4096), DIGITOR_BUFFER_USAGE_STORAGE | DIGITOR_BUFFER_USAGE_UPLOAD
    };

    assert(texture.width == 7u);
    assert(texture.height == 5u);
    assert(texture.format == DIGITOR_PIXEL_FORMAT_RGBA8_UNORM);
    assert(texture.usage == (DIGITOR_TEXTURE_USAGE_SAMPLED |
                             DIGITOR_TEXTURE_USAGE_TRANSFER_DESTINATION));
    assert(buffer.size == UINT64_C(4096));
    assert(buffer.usage == (DIGITOR_BUFFER_USAGE_STORAGE | DIGITOR_BUFFER_USAGE_UPLOAD));
    DigitorResult (*map_buffer)(DigitorBuffer*, uint64_t, uint64_t, void**) = digitor_map_buffer;
    DigitorResult (*unmap_buffer)(DigitorBuffer*) = digitor_unmap_buffer;
    (void)map_buffer;
    (void)unmap_buffer;

    /* Referencing these values verifies that the public enums remain available to C. */
    assert(DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT);
    assert(DIGITOR_RENDERER_CPU != DIGITOR_RENDERER_AUTO);

    DigitorNodeGraph* graph = 0;
    assert(digitor_node_graph_create(&graph) == DIGITOR_RESULT_OK);
    DigitorNodeId input = 0, output = 0, grade = 0;
    assert(digitor_node_graph_get_endpoints(graph, &input, &output) == DIGITOR_RESULT_OK);
    assert(input != 0 && output != 0);
    assert(digitor_node_graph_add_serial_after(graph, input, "Grade 1", &grade) == DIGITOR_RESULT_OK);
    assert(digitor_node_graph_select(graph, grade) == DIGITOR_RESULT_OK);
    DigitorPrimaryWheelsControls wheels = {0};
    wheels.gamma.r = wheels.gamma.g = wheels.gamma.b = 1.0f; wheels.gamma_master = 1.0f; wheels.gamma_enabled = 1;
    wheels.gain.r = wheels.gain.g = wheels.gain.b = 1.0f; wheels.gain_master = 1.0f; wheels.gain_enabled = 1;
    wheels.lift_enabled = wheels.offset_enabled = 1; wheels.offset.r = 0.1f;
    assert(digitor_node_graph_add_primary_wheels(graph, &wheels) == DIGITOR_RESULT_OK);
    DigitorCurvePoint identity_points[2] = {{0.0f,0.0f},{1.0f,1.0f}};
    DigitorCurveChannel identity_channel = {identity_points,2,1};
    DigitorRgbCurvesControls curves = {identity_channel,identity_channel,identity_channel,identity_channel,256};
    assert(digitor_node_graph_add_rgb_curves(graph, &curves) == DIGITOR_RESULT_OK);
    DigitorHslQualifierControls qualifier = {{0.0f,1.0f,0.0f},{0.0f,1.0f,0.0f},{0.0f,1.0f,0.0f},0,0,0,0,0,0};
    assert(digitor_node_graph_add_hsl_qualifier(graph, &qualifier) == DIGITOR_RESULT_OK);
    DigitorLutColor lut_values[2] = {{0,0,0,1},{1,1,1,1}};
    DigitorLut1DControls lut1d = {lut_values,2,DIGITOR_LUT_LINEAR};
    assert(digitor_node_graph_add_lut1d(graph, &lut1d) == DIGITOR_RESULT_OK);
    DigitorNodeEffectSettings effect = {DIGITOR_NODE_EFFECT_VIGNETTE,0.25f,0.5f,0.0f,1};
    assert(digitor_node_graph_add_effect(graph, &effect) == DIGITOR_RESULT_OK);
    DigitorPowerWindowSettings window = {DIGITOR_WINDOW_ELLIPSE,0.5f,0.5f,0.8f,0.8f,0.0f,0.1f,1.0f,0};
    assert(digitor_node_graph_add_power_window(graph, &window) == DIGITOR_RESULT_OK);
    uint64_t required = 0;
    assert(digitor_node_graph_to_json(graph, 0, 0, &required) == DIGITOR_RESULT_OK && required > 2);
    char* json = (char*)malloc((size_t)required);
    assert(json != 0 && digitor_node_graph_to_json(graph, json, required, &required) == DIGITOR_RESULT_OK);
    assert(strstr(json, "Grade 1") != 0); free(json);
    assert(digitor_node_graph_destroy(graph) == DIGITOR_RESULT_OK);

    return 0;
}
