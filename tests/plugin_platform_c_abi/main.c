#include "digitor/plugin_platform_c.h"

#include <stdio.h>
#include <string.h>

typedef struct State {
    unsigned single_calls;
    unsigned transition_calls;
} State;

static DigitorResult process_single(void* user_data,
                                    const DigitorPluginRequestC* request,
                                    char* diagnostic,
                                    size_t diagnostic_capacity) {
    State* state = (State*)user_data;
    if (!state || request->kind == DIGITOR_PLUGIN_KIND_TRANSITION ||
        request->input.native_texture_handle != 10 ||
        request->output.native_texture_handle != 12) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    ++state->single_calls;
    if (diagnostic && diagnostic_capacity) diagnostic[0] = '\0';
    return DIGITOR_RESULT_OK;
}

static DigitorResult process_transition(void* user_data,
                                        const DigitorPluginRequestC* request,
                                        char* diagnostic,
                                        size_t diagnostic_capacity) {
    State* state = (State*)user_data;
    if (!state || request->kind != DIGITOR_PLUGIN_KIND_TRANSITION ||
        request->input.native_texture_handle != 10 ||
        request->incoming.native_texture_handle != 11 ||
        request->output.native_texture_handle != 12 ||
        request->transition_progress != 0.5) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    ++state->transition_calls;
    if (diagnostic && diagnostic_capacity) diagnostic[0] = '\0';
    return DIGITOR_RESULT_OK;
}

static DigitorPluginFrameC frame(uint64_t handle) {
    DigitorPluginFrameC value;
    memset(&value, 0, sizeof(value));
    value.native_texture_handle = handle;
    value.width = 1920;
    value.height = 1080;
    value.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
    return value;
}

int main(void) {
    State state = {0, 0};
    DigitorPluginPlatformBindingsC bindings;
    memset(&bindings, 0, sizeof(bindings));
    bindings.struct_size = sizeof(bindings);
    bindings.user_data = &state;
    bindings.process_single_input = process_single;
    bindings.process_transition = process_transition;

    DigitorPluginPlatformC* platform = NULL;
    if (digitor_plugin_platform_create(&bindings, &platform) != DIGITOR_RESULT_OK || !platform)
        return 1;

    DigitorPluginParameterC parameter = {"strength", 0.75};
    DigitorPluginRequestC request;
    memset(&request, 0, sizeof(request));
    request.struct_size = sizeof(request);
    request.kind = DIGITOR_PLUGIN_KIND_EFFECT;
    request.surface = DIGITOR_PLUGIN_SURFACE_PREVIEW;
    request.instance_id = "instance.1";
    request.plugin_id = "effect.website.package";
    request.plugin_version = "1.0.0";
    request.project_or_clip_id = "clip.1";
    request.parameters = &parameter;
    request.parameter_count = 1;
    request.input = frame(10);
    request.output = frame(12);

    char diagnostic[128];
    if (digitor_plugin_platform_process(platform, &request, diagnostic,
                                        sizeof(diagnostic)) != DIGITOR_RESULT_OK)
        return 2;

    request.kind = DIGITOR_PLUGIN_KIND_TRANSITION;
    request.surface = DIGITOR_PLUGIN_SURFACE_EXPORT;
    request.incoming = frame(11);
    request.transition_progress = 0.5;
    if (digitor_plugin_platform_process(platform, &request, diagnostic,
                                        sizeof(diagnostic)) != DIGITOR_RESULT_OK)
        return 3;

    request.transition_progress = 1.5;
    if (digitor_plugin_platform_process(platform, &request, diagnostic,
                                        sizeof(diagnostic)) == DIGITOR_RESULT_OK)
        return 4;

    digitor_plugin_platform_destroy(platform);
    if (state.single_calls != 1 || state.transition_calls != 1 ||
        digitor_plugin_platform_c_abi_version() != 1u)
        return 5;

    puts("PLUGIN_PLATFORM_C_ABI_QUALIFIED=1");
    puts("PURE_C_CONSUMER=1");
    puts("FILTER_EFFECT_TRANSITION_ROUTING=1");
    return 0;
}
