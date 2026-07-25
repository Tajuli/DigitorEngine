#include "digitor/digitor.h"

#include "core/engine.hpp"

struct DigitorRenderContext {
    digitor::RenderContext* impl;
};

const char* digitor_get_version(void) {
    return "0.2.0";
}

DigitorResult digitor_initialize(const DigitorEngineConfig* config) {
    DigitorEngineConfig resolved{};
    resolved.preferred_backend = DIGITOR_RENDERER_AUTO;
    resolved.enable_validation = 0;
    resolved.allow_cpu_fallback = 1;

    if (config != nullptr) {
        resolved = *config;
    }

    try {
        return digitor::Engine::instance().initialize(resolved);
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_shutdown(void) {
    try {
        return digitor::Engine::instance().shutdown();
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_get_renderer_info(DigitorRendererInfo* out_info) {
    if (out_info == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    try {
        return digitor::Engine::instance().renderer_info(out_info);
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_create_render_context(
    DigitorRenderContext** out_context
) {
    if (out_context == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    *out_context = nullptr;

    digitor::RenderContext* internal = nullptr;
    try {
        const DigitorResult result =
            digitor::Engine::instance().create_context(&internal);

        if (result != DIGITOR_RESULT_OK) {
            return result;
        }

        auto* wrapper = new DigitorRenderContext{};
        wrapper->impl = internal;
        *out_context = wrapper;
        return DIGITOR_RESULT_OK;
    } catch (...) {
        if (internal != nullptr) {
            (void)digitor::Engine::instance().destroy_context(internal);
        }
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_destroy_render_context(
    DigitorRenderContext* context
) {
    if (context == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    try {
        const DigitorResult result =
            digitor::Engine::instance().destroy_context(context->impl);

        delete context;
        return result;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}
