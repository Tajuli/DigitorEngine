#include "digitor/digitor.h"

#include <cstring>
#include <new>
#include <mutex>
#include <unordered_set>

#include "core/engine.hpp"
#include "core/resources.hpp"

struct DigitorRenderContext {
    digitor::RenderContext* impl;
};

struct DigitorTexture { digitor::Texture* impl; };
struct DigitorBuffer { digitor::Buffer* impl; };
struct DigitorSampler { digitor::Sampler* impl; };
namespace { std::mutex handles_mutex; std::unordered_set<void*> contexts, textures, buffers, samplers;
bool retire(std::unordered_set<void*>& set, void* value) { std::scoped_lock lock(handles_mutex); return set.erase(value) == 1; }
void register_handle(std::unordered_set<void*>& set, void* value) { std::scoped_lock lock(handles_mutex); set.insert(value); }
bool registered(const std::unordered_set<void*>& set, const void* value) { std::scoped_lock lock(handles_mutex); return set.count(const_cast<void*>(value)) != 0; }
}

const char* digitor_get_version(void) {
    return "4.2.0";
}

DigitorResult digitor_create_texture(DigitorRenderContext* context, const DigitorTextureDesc* desc,
                                     DigitorTexture** out_texture) {
    if (context == nullptr || desc == nullptr || out_texture == nullptr || !registered(contexts, context)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_texture = nullptr;
    digitor::Texture* resource = nullptr;
    const auto result = context->impl->create_texture(*desc, &resource);
    if (result != DIGITOR_RESULT_OK) return result;
    try { *out_texture = new DigitorTexture{resource}; register_handle(textures, *out_texture); }
    catch (const std::bad_alloc&) { delete resource; return DIGITOR_RESULT_OUT_OF_MEMORY; }
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_destroy_texture(DigitorTexture* texture) {
    if (texture == nullptr || !retire(textures, texture)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    delete texture->impl;
    delete texture;
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_create_buffer(DigitorRenderContext* context, const DigitorBufferDesc* desc,
                                    DigitorBuffer** out_buffer) {
    if (context == nullptr || desc == nullptr || out_buffer == nullptr || !registered(contexts, context)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_buffer = nullptr;
    digitor::Buffer* resource = nullptr;
    const auto result = context->impl->create_buffer(*desc, &resource);
    if (result != DIGITOR_RESULT_OK) return result;
    try { *out_buffer = new DigitorBuffer{resource}; register_handle(buffers, *out_buffer); }
    catch (const std::bad_alloc&) { delete resource; return DIGITOR_RESULT_OUT_OF_MEMORY; }
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_destroy_buffer(DigitorBuffer* buffer) {
    if (buffer == nullptr || !retire(buffers, buffer)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    delete buffer->impl;
    delete buffer;
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_map_buffer(DigitorBuffer* buffer, uint64_t offset, uint64_t size,
                                 void** out_data) {
    if (!buffer || !out_data) return DIGITOR_RESULT_INVALID_ARGUMENT;
    { std::scoped_lock lock(handles_mutex); if (!buffers.count(buffer)) return DIGITOR_RESULT_INVALID_ARGUMENT; }
    return buffer->impl->map(offset, size, out_data);
}

DigitorResult digitor_unmap_buffer(DigitorBuffer* buffer) {
    if (!buffer) return DIGITOR_RESULT_INVALID_ARGUMENT;
    { std::scoped_lock lock(handles_mutex); if (!buffers.count(buffer)) return DIGITOR_RESULT_INVALID_ARGUMENT; }
    return buffer->impl->unmap();
}

DigitorResult digitor_create_sampler(DigitorRenderContext* context, const DigitorSamplerDesc* desc,
                                     DigitorSampler** out_sampler) {
    if (!context || !desc || !out_sampler || !registered(contexts, context)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_sampler = nullptr; digitor::Sampler* resource = nullptr;
    auto result = context->impl->create_sampler(*desc, &resource); if (result != DIGITOR_RESULT_OK) return result;
    try { *out_sampler = new DigitorSampler{resource}; register_handle(samplers, *out_sampler); }
    catch (const std::bad_alloc&) { delete resource; return DIGITOR_RESULT_OUT_OF_MEMORY; }
    return DIGITOR_RESULT_OK;
}
DigitorResult digitor_destroy_sampler(DigitorSampler* sampler) {
    if (!sampler || !retire(samplers, sampler)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    delete sampler->impl;
    delete sampler;
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

    auto* wrapper = new (std::nothrow) DigitorRenderContext{};
    if (!wrapper) { (void)digitor::Engine::instance().destroy_context(internal); return DIGITOR_RESULT_OUT_OF_MEMORY; }
    wrapper->impl = internal;
    *out_context = wrapper;
    register_handle(contexts, wrapper);
    return DIGITOR_RESULT_OK;
}

DigitorResult digitor_destroy_render_context(
    DigitorRenderContext* context
) {
    if (context == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    { std::scoped_lock lock(handles_mutex); if (!contexts.count(context)) return DIGITOR_RESULT_INVALID_ARGUMENT; }

    const DigitorResult result =
        digitor::Engine::instance().destroy_context(context->impl);

    if (result == DIGITOR_RESULT_OK) {
        (void)retire(contexts, context);
        delete context;
    }
    return result;
}
