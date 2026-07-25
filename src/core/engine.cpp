#include "core/engine.hpp"

#include "cpu/cpu_backend.hpp"
#include "platform/platform.hpp"

namespace digitor {

Engine& Engine::instance() {
    static Engine engine;
    return engine;
}

DigitorResult Engine::initialize(const DigitorEngineConfig& config) {
    std::scoped_lock lock(mutex_);

    if (initialized_) {
        return DIGITOR_RESULT_ALREADY_INITIALIZED;
    }

    if (config.preferred_backend != DIGITOR_RENDERER_AUTO &&
        config.preferred_backend != DIGITOR_RENDERER_VULKAN &&
        config.preferred_backend != DIGITOR_RENDERER_METAL &&
        config.preferred_backend != DIGITOR_RENDERER_D3D12 &&
        config.preferred_backend != DIGITOR_RENDERER_OPENGL_ES &&
        config.preferred_backend != DIGITOR_RENDERER_CPU) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    config_ = config;

    if (config.preferred_backend == DIGITOR_RENDERER_CPU) {
        backend_ = std::make_unique<CpuBackend>();
    } else {
        backend_ = create_gpu_backend(config.preferred_backend);
    }

    if (!backend_ && config.allow_cpu_fallback) {
        backend_ = std::make_unique<CpuBackend>();
    }

    if (!backend_) {
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    if (!backend_->initialize(config.enable_validation != 0)) {
        backend_.reset();
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    initialized_ = true;
    return DIGITOR_RESULT_OK;
}

DigitorResult Engine::shutdown() {
    std::scoped_lock lock(mutex_);

    if (!initialized_) {
        return DIGITOR_RESULT_NOT_INITIALIZED;
    }

    if (backend_) {
        for (RenderContext* context : contexts_) {
            delete context;
        }
        contexts_.clear();
        backend_->shutdown();
        backend_.reset();
    }

    initialized_ = false;
    return DIGITOR_RESULT_OK;
}

bool Engine::is_initialized() const noexcept {
    std::scoped_lock lock(mutex_);
    return initialized_;
}

DigitorResult Engine::renderer_info(DigitorRendererInfo* out_info) const noexcept {
    if (out_info == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(mutex_);

    if (!initialized_ || !backend_) {
        return DIGITOR_RESULT_NOT_INITIALIZED;
    }

    *out_info = backend_->info();
    return DIGITOR_RESULT_OK;
}

DigitorResult Engine::create_context(RenderContext** out_context) {
    if (out_context == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(mutex_);

    if (!initialized_ || !backend_) {
        return DIGITOR_RESULT_NOT_INITIALIZED;
    }

    auto* context = new RenderContext(backend_->info().backend);
    contexts_.insert(context);
    *out_context = context;
    return DIGITOR_RESULT_OK;
}

DigitorResult Engine::destroy_context(RenderContext* context) {
    if (context == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(mutex_);
    const auto entry = contexts_.find(context);
    if (entry == contexts_.end()) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    delete *entry;
    contexts_.erase(entry);
    return DIGITOR_RESULT_OK;
}

}  // namespace digitor
