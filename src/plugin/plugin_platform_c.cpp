#include "digitor/plugin_platform_c.h"

#include <algorithm>
#include <cstring>
#include <new>

struct DigitorPluginPlatformC {
    DigitorPluginPlatformBindingsC bindings{};
};

namespace {
void write_diagnostic(char* output, size_t capacity, const char* message) noexcept {
    if (!output || capacity == 0) return;
    const char* text = message ? message : "";
    const size_t count = std::min(capacity - 1, std::strlen(text));
    std::memcpy(output, text, count);
    output[count] = '\0';
}

bool valid_text(const char* value) noexcept {
    return value && value[0] != '\0';
}

bool valid_frame(const DigitorPluginFrameC& frame) noexcept {
    return frame.native_texture_handle != 0 && frame.width != 0 && frame.height != 0;
}

DigitorResult validate_request(const DigitorPluginRequestC* request,
                               char* diagnostic,
                               size_t diagnostic_capacity) noexcept {
    if (!request || request->struct_size < sizeof(DigitorPluginRequestC) ||
        !valid_text(request->instance_id) || !valid_text(request->plugin_id) ||
        !valid_text(request->plugin_version) ||
        !valid_text(request->project_or_clip_id) || !valid_frame(request->output)) {
        write_diagnostic(diagnostic, diagnostic_capacity,
                         "plugin C ABI request metadata is invalid");
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    if (request->parameter_count > 256 ||
        (request->parameter_count != 0 && !request->parameters)) {
        write_diagnostic(diagnostic, diagnostic_capacity,
                         "plugin C ABI parameter array is invalid");
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < request->parameter_count; ++i) {
        if (!valid_text(request->parameters[i].id)) {
            write_diagnostic(diagnostic, diagnostic_capacity,
                             "plugin C ABI parameter id is invalid");
            return DIGITOR_RESULT_INVALID_ARGUMENT;
        }
    }
    if (!valid_frame(request->input)) {
        write_diagnostic(diagnostic, diagnostic_capacity,
                         "plugin C ABI input frame is invalid");
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    if (request->kind == DIGITOR_PLUGIN_KIND_TRANSITION) {
        if (!valid_frame(request->incoming) || request->transition_progress < 0.0 ||
            request->transition_progress > 1.0) {
            write_diagnostic(diagnostic, diagnostic_capacity,
                             "plugin C ABI transition request is invalid");
            return DIGITOR_RESULT_INVALID_ARGUMENT;
        }
    } else if (request->kind != DIGITOR_PLUGIN_KIND_FILTER &&
               request->kind != DIGITOR_PLUGIN_KIND_EFFECT) {
        write_diagnostic(diagnostic, diagnostic_capacity,
                         "plugin C ABI kind is unsupported");
        return DIGITOR_RESULT_UNSUPPORTED;
    }
    return DIGITOR_RESULT_OK;
}
}  // namespace

extern "C" DigitorResult digitor_plugin_platform_create(
    const DigitorPluginPlatformBindingsC* bindings,
    DigitorPluginPlatformC** out_platform) {
    if (out_platform) *out_platform = nullptr;
    if (!bindings || !out_platform ||
        bindings->struct_size < sizeof(DigitorPluginPlatformBindingsC) ||
        !bindings->process_single_input || !bindings->process_transition) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    try {
        auto* platform = new DigitorPluginPlatformC{};
        platform->bindings = *bindings;
        *out_platform = platform;
        return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

extern "C" void digitor_plugin_platform_destroy(
    DigitorPluginPlatformC* platform) {
    delete platform;
}

extern "C" DigitorResult digitor_plugin_platform_process(
    DigitorPluginPlatformC* platform,
    const DigitorPluginRequestC* request,
    char* diagnostic,
    size_t diagnostic_capacity) {
    write_diagnostic(diagnostic, diagnostic_capacity, "");
    if (!platform) {
        write_diagnostic(diagnostic, diagnostic_capacity,
                         "plugin C ABI platform is unavailable");
        return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    const auto validation = validate_request(request, diagnostic,
                                             diagnostic_capacity);
    if (validation != DIGITOR_RESULT_OK) return validation;
    try {
        const auto callback = request->kind == DIGITOR_PLUGIN_KIND_TRANSITION
            ? platform->bindings.process_transition
            : platform->bindings.process_single_input;
        return callback(platform->bindings.user_data, request, diagnostic,
                        diagnostic_capacity);
    } catch (...) {
        write_diagnostic(diagnostic, diagnostic_capacity,
                         "plugin C ABI callback raised an exception");
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

extern "C" uint32_t digitor_plugin_platform_c_abi_version(void) {
    return 1u;
}
