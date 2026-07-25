#include "digitor/digitor.h"

#include <cstring>

#include "core/engine.hpp"

struct DigitorRenderContext {
    digitor::RenderContext* impl;
};

const char* digitor_get_version(void) {
    return "0.1.0";
}

DigitorResult digitor_initialize(const DigitorEngineConfig* config) {
    DigitorEngineConfig resolved{};
    resolved.preferred_backend = DIGITOR_RENDERER_AUTO;
    resolved.enable_validation = 0;
    resolved.allow_cpu_fallback = 1;

    if (config != nullptr) {
        resolved = *config;
    }

    return digitor::Engine::instance().initialize(resolved);
}

DigitorResult digitor_shutdown(void) {
    return digitor::Engine::instance().shutdown();
}

DigitorResult digitor_get_renderer_info(DigitorRendererInfo* out_info) {
    if (out_info == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    if (!digitor::Engine::instance().is_initialized()) {
        return DIGITOR_RESULT_NOT_INITIALIZED;
    }

    *out_info = digitor::Engine::instance().renderer_info();
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_create_render_context(
    DigitorRenderContext** out_context
) {
    if (out_context == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    digitor::RenderContext* internal = nullptr;
    const DigitorResult result =
        digitor::Engine::instance().create_context(&internal);

    if (result != DIGITOR_RESULT_OK) {
        return result;
    }

    auto* wrapper = new DigitorRenderContext{};
    wrapper->impl = internal;
    *out_context = wrapper;
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_destroy_render_context(
    DigitorRenderContext* context
) {
    if (context == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    const DigitorResult result =
        digitor::Engine::instance().destroy_context(context->impl);

    delete context;
    return result;
}
