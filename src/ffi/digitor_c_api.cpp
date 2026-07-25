#include "digitor/digitor.h"

#include <cstring>
#include <new>

#include "core/engine.hpp"
#include "core/resources.hpp"

struct DigitorRenderContext {
    digitor::RenderContext* impl;
};

struct DigitorTexture { digitor::Texture* impl; };
struct DigitorBuffer { digitor::Buffer* impl; };

const char* digitor_get_version(void) {
    return "0.2.0";
}

DigitorResult digitor_create_texture(DigitorRenderContext* context, const DigitorTextureDesc* desc,
                                     DigitorTexture** out_texture) {
    if (context == nullptr || desc == nullptr || out_texture == nullptr) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_texture = nullptr;
    digitor::Texture* resource = nullptr;
    const auto result = context->impl->create_texture(*desc, &resource);
    if (result != DIGITOR_RESULT_OK) return result;
    try { *out_texture = new DigitorTexture{resource}; }
    catch (const std::bad_alloc&) { delete resource; return DIGITOR_RESULT_OUT_OF_MEMORY; }
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_destroy_texture(DigitorTexture* texture) {
    if (texture == nullptr) return DIGITOR_RESULT_INVALID_ARGUMENT;
    delete texture->impl;
    delete texture;
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_create_buffer(DigitorRenderContext* context, const DigitorBufferDesc* desc,
                                    DigitorBuffer** out_buffer) {
    if (context == nullptr || desc == nullptr || out_buffer == nullptr) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_buffer = nullptr;
    digitor::Buffer* resource = nullptr;
    const auto result = context->impl->create_buffer(*desc, &resource);
    if (result != DIGITOR_RESULT_OK) return result;
    try { *out_buffer = new DigitorBuffer{resource}; }
    catch (const std::bad_alloc&) { delete resource; return DIGITOR_RESULT_OUT_OF_MEMORY; }
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_destroy_buffer(DigitorBuffer* buffer) {
    if (buffer == nullptr) return DIGITOR_RESULT_INVALID_ARGUMENT;
    delete buffer->impl;
    delete buffer;
    return DIGITOR_RESULT_OK;
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

    if (result == DIGITOR_RESULT_OK) {
        delete context;
    }
    return result;
}
