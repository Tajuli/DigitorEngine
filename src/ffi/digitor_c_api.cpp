#include "digitor/digitor.h"

#include <mutex>
#include <new>
#include <unordered_set>

#include "core/engine.hpp"
#include "core/resources.hpp"

struct DigitorRenderContext { digitor::RenderContext* impl; };
struct DigitorTexture { digitor::Texture* impl; };
struct DigitorBuffer { digitor::Buffer* impl; };
struct DigitorSampler { digitor::Sampler* impl; };

namespace {
std::recursive_mutex g_handles_mutex;
std::unordered_set<void*> g_contexts;
std::unordered_set<void*> g_textures;
std::unordered_set<void*> g_buffers;
std::unordered_set<void*> g_samplers;

template <class Fn>
DigitorResult c_api_guard(Fn&& fn) noexcept {
    try { return fn(); }
    catch (const std::bad_alloc&) { return DIGITOR_RESULT_OUT_OF_MEMORY; }
    catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

template <class T>
bool contains(const std::unordered_set<void*>& set, const T* value) noexcept {
    return value != nullptr && set.contains(const_cast<T*>(value));
}
} // namespace

extern "C" {

const char* digitor_get_version(void) { return "4.9.0"; }

DigitorResult digitor_initialize(const DigitorEngineConfig* config) {
    return c_api_guard([&] {
        DigitorEngineConfig resolved{};
        resolved.preferred_backend = DIGITOR_RENDERER_AUTO;
        resolved.enable_validation = 0;
        resolved.allow_cpu_fallback = 1;
        if (config) resolved = *config;
        return digitor::Engine::instance().initialize(resolved);
    });
}

DigitorResult digitor_shutdown(void) {
    return c_api_guard([] { return digitor::Engine::instance().shutdown(); });
}

DigitorResult digitor_get_renderer_info(DigitorRendererInfo* out_info) {
    if (!out_info) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_info = {};
    return c_api_guard([&] {
        if (!digitor::Engine::instance().is_initialized()) return DIGITOR_RESULT_NOT_INITIALIZED;
        *out_info = digitor::Engine::instance().renderer_info();
        return DIGITOR_RESULT_OK;
    });
}

DigitorResult digitor_create_render_context(DigitorRenderContext** out_context) {
    if (!out_context) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_context = nullptr;
    return c_api_guard([&] {
        std::scoped_lock lock(g_handles_mutex);
        digitor::RenderContext* internal = nullptr;
        const auto result = digitor::Engine::instance().create_context(&internal);
        if (result != DIGITOR_RESULT_OK) return result;
        auto* wrapper = new (std::nothrow) DigitorRenderContext{internal};
        if (!wrapper) {
            (void)digitor::Engine::instance().destroy_context(internal);
            return DIGITOR_RESULT_OUT_OF_MEMORY;
        }
        g_contexts.insert(wrapper);
        *out_context = wrapper;
        return DIGITOR_RESULT_OK;
    });
}

DigitorResult digitor_destroy_render_context(DigitorRenderContext* context) {
    return c_api_guard([&] {
        std::scoped_lock lock(g_handles_mutex);
        if (!contains(g_contexts, context)) return DIGITOR_RESULT_INVALID_ARGUMENT;
        const auto result = digitor::Engine::instance().destroy_context(context->impl);
        if (result == DIGITOR_RESULT_OK) {
            g_contexts.erase(context);
            delete context;
        }
        return result;
    });
}

DigitorResult digitor_create_texture(DigitorRenderContext* context,
                                     const DigitorTextureDesc* desc,
                                     DigitorTexture** out_texture) {
    if (!out_texture) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_texture = nullptr;
    return c_api_guard([&] {
        std::scoped_lock lock(g_handles_mutex);
        if (!desc || !contains(g_contexts, context)) return DIGITOR_RESULT_INVALID_ARGUMENT;
        digitor::Texture* resource = nullptr;
        const auto result = context->impl->create_texture(*desc, &resource);
        if (result != DIGITOR_RESULT_OK) return result;
        auto* wrapper = new (std::nothrow) DigitorTexture{resource};
        if (!wrapper) { delete resource; return DIGITOR_RESULT_OUT_OF_MEMORY; }
        g_textures.insert(wrapper);
        *out_texture = wrapper;
        return DIGITOR_RESULT_OK;
    });
}

DigitorResult digitor_destroy_texture(DigitorTexture* texture) {
    return c_api_guard([&] {
        std::scoped_lock lock(g_handles_mutex);
        if (!contains(g_textures, texture)) return DIGITOR_RESULT_INVALID_ARGUMENT;
        g_textures.erase(texture);
        delete texture->impl;
        delete texture;
        return DIGITOR_RESULT_OK;
    });
}

DigitorResult digitor_create_buffer(DigitorRenderContext* context,
                                    const DigitorBufferDesc* desc,
                                    DigitorBuffer** out_buffer) {
    if (!out_buffer) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_buffer = nullptr;
    return c_api_guard([&] {
        std::scoped_lock lock(g_handles_mutex);
        if (!desc || !contains(g_contexts, context)) return DIGITOR_RESULT_INVALID_ARGUMENT;
        digitor::Buffer* resource = nullptr;
        const auto result = context->impl->create_buffer(*desc, &resource);
        if (result != DIGITOR_RESULT_OK) return result;
        auto* wrapper = new (std::nothrow) DigitorBuffer{resource};
        if (!wrapper) { delete resource; return DIGITOR_RESULT_OUT_OF_MEMORY; }
        g_buffers.insert(wrapper);
        *out_buffer = wrapper;
        return DIGITOR_RESULT_OK;
    });
}

DigitorResult digitor_destroy_buffer(DigitorBuffer* buffer) {
    return c_api_guard([&] {
        std::scoped_lock lock(g_handles_mutex);
        if (!contains(g_buffers, buffer)) return DIGITOR_RESULT_INVALID_ARGUMENT;
        g_buffers.erase(buffer);
        delete buffer->impl;
        delete buffer;
        return DIGITOR_RESULT_OK;
    });
}

DigitorResult digitor_map_buffer(DigitorBuffer* buffer, uint64_t offset,
                                 uint64_t size, void** out_data) {
    if (!out_data) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_data = nullptr;
    return c_api_guard([&] {
        std::scoped_lock lock(g_handles_mutex);
        if (!contains(g_buffers, buffer)) return DIGITOR_RESULT_INVALID_ARGUMENT;
        return buffer->impl->map(offset, size, out_data);
    });
}

DigitorResult digitor_unmap_buffer(DigitorBuffer* buffer) {
    return c_api_guard([&] {
        std::scoped_lock lock(g_handles_mutex);
        if (!contains(g_buffers, buffer)) return DIGITOR_RESULT_INVALID_ARGUMENT;
        return buffer->impl->unmap();
    });
}

DigitorResult digitor_create_sampler(DigitorRenderContext* context,
                                     const DigitorSamplerDesc* desc,
                                     DigitorSampler** out_sampler) {
    if (!out_sampler) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_sampler = nullptr;
    return c_api_guard([&] {
        std::scoped_lock lock(g_handles_mutex);
        if (!desc || !contains(g_contexts, context)) return DIGITOR_RESULT_INVALID_ARGUMENT;
        digitor::Sampler* resource = nullptr;
        const auto result = context->impl->create_sampler(*desc, &resource);
        if (result != DIGITOR_RESULT_OK) return result;
        auto* wrapper = new (std::nothrow) DigitorSampler{resource};
        if (!wrapper) { delete resource; return DIGITOR_RESULT_OUT_OF_MEMORY; }
        g_samplers.insert(wrapper);
        *out_sampler = wrapper;
        return DIGITOR_RESULT_OK;
    });
}

DigitorResult digitor_destroy_sampler(DigitorSampler* sampler) {
    return c_api_guard([&] {
        std::scoped_lock lock(g_handles_mutex);
        if (!contains(g_samplers, sampler)) return DIGITOR_RESULT_INVALID_ARGUMENT;
        g_samplers.erase(sampler);
        delete sampler->impl;
        delete sampler;
        return DIGITOR_RESULT_OK;
    });
}

} // extern "C"
