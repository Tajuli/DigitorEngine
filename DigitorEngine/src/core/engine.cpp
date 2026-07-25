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

    config_ = config;

    backend_ = create_gpu_backend(config.preferred_backend);

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

DigitorRendererInfo Engine::renderer_info() const noexcept {
    std::scoped_lock lock(mutex_);

    if (!backend_) {
        return {};
    }

    return backend_->info();
}

DigitorResult Engine::create_context(RenderContext** out_context) {
    if (out_context == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    std::scoped_lock lock(mutex_);

    if (!initialized_ || !backend_) {
        return DIGITOR_RESULT_NOT_INITIALIZED;
    }

    *out_context = new RenderContext(backend_->info().backend);
    return DIGITOR_RESULT_OK;
}

DigitorResult Engine::destroy_context(RenderContext* context) {
    if (context == nullptr) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    delete context;
    return DIGITOR_RESULT_OK;
}

}  // namespace digitor
