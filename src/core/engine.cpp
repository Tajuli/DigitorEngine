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
    if (initialized_) return DIGITOR_RESULT_ALREADY_INITIALIZED;

    const bool valid =
        config.preferred_backend == DIGITOR_RENDERER_AUTO ||
        config.preferred_backend == DIGITOR_RENDERER_VULKAN ||
        config.preferred_backend == DIGITOR_RENDERER_METAL ||
        config.preferred_backend == DIGITOR_RENDERER_D3D12 ||
        config.preferred_backend == DIGITOR_RENDERER_OPENGL_ES ||
        config.preferred_backend == DIGITOR_RENDERER_CPU;
    if (!valid || config.enable_validation > 1 || config.allow_cpu_fallback > 1) {
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    config_ = config;
    const bool explicit_cpu = config.preferred_backend == DIGITOR_RENDERER_CPU;
    const bool automatic = config.preferred_backend == DIGITOR_RENDERER_AUTO;

    if (explicit_cpu) {
        backend_ = std::make_unique<CpuBackend>();
    } else {
        backend_ = create_gpu_backend(config.preferred_backend);
    }

    if (backend_ && backend_->initialize(config.enable_validation != 0)) {
        initialized_ = true;
        return DIGITOR_RESULT_OK;
    }

    if (backend_) {
        backend_->shutdown();
        backend_.reset();
    }

    // CPU is selected only when AUTO cannot establish a usable GPU backend.
    // Explicit GPU requests stay strict. Once initialized, the selected backend
    // is locked for the engine lifetime and runtime failures never switch it.
    if (automatic && config.allow_cpu_fallback) {
        auto cpu = std::make_unique<CpuBackend>();
        if (!cpu->initialize(config.enable_validation != 0)) {
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        }
        backend_ = std::move(cpu);
        initialized_ = true;
        return DIGITOR_RESULT_OK;
    }

    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
}

DigitorResult Engine::shutdown() {
    std::scoped_lock lock(mutex_);
    if (!initialized_) return DIGITOR_RESULT_NOT_INITIALIZED;
    if (!contexts_.empty()) return DIGITOR_RESULT_RESOURCE_IN_USE;
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
    return backend_ ? backend_->info() : DigitorRendererInfo{};
}

DigitorResult Engine::create_context(RenderContext** out) {
    if (!out) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out = nullptr;
    std::scoped_lock lock(mutex_);
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    try {
        *out = new RenderContext(*backend_);
        contexts_.insert(*out);
    } catch (const std::bad_alloc&) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    return DIGITOR_RESULT_OK;
}

DigitorResult Engine::destroy_context(RenderContext* context) {
    if (!context) return DIGITOR_RESULT_INVALID_ARGUMENT;
    std::scoped_lock lock(mutex_);
    if (!contexts_.contains(context)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (context->has_resources()) return DIGITOR_RESULT_RESOURCE_IN_USE;
    contexts_.erase(context);
    delete context;
    return DIGITOR_RESULT_OK;
}

DigitorResult Engine::render_preview_rgba8(uint32_t width, uint32_t height,
                                            std::span<const uint8_t> source,
                                            std::vector<uint8_t>& destination) {
    std::scoped_lock lock(mutex_);
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->render_rgba8(width, height, source, destination);
}

DigitorResult Engine::grade_rgba32f(std::span<const Color> source,
                                     std::span<Color> destination,
                                     const ColorGrade& parameters) {
    std::scoped_lock lock(mutex_);
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->grade_rgba32f(source, destination, parameters);
}

DigitorResult Engine::curves_rgba32f(std::span<const Color> source,
                                      std::span<Color> destination,
                                      const CompiledRgbCurves& curves) {
    std::scoped_lock lock(mutex_);
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->curves_rgba32f(source, destination, curves);
}

DigitorResult Engine::process_curves_gpu(std::span<const Color> source,
                                          std::uint32_t width,
                                          std::uint32_t height,
                                          std::int64_t timestamp,
                                          const CompiledRgbCurves& curves,
                                          ProcessedGpuFramePtr& output) {
    std::scoped_lock lock(mutex_);
    output.reset();
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->process_curves_gpu(source, width, height, timestamp, curves,
                                        output);
}

DigitorResult Engine::process_primary_wheels_gpu(
    std::span<const Color> source, std::uint32_t width, std::uint32_t height,
    std::int64_t timestamp, const PrimaryWheelsParameters& parameters,
    ProcessedGpuFramePtr& output) {
    std::lock_guard lock(mutex_);
    output.reset();
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->process_primary_wheels_gpu(source, width, height, timestamp,
                                                parameters, output);
}

DigitorResult Engine::process_curves_gpu(const ProcessedGpuFramePtr& source,
                                          std::int64_t timestamp,
                                          const CompiledRgbCurves& curves,
                                          ProcessedGpuFramePtr& output) {
    std::lock_guard lock(mutex_);
    output.reset();
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->process_curves_gpu(backend_->gpu_source(source), timestamp,
                                        curves, output);
}

DigitorResult Engine::process_primary_wheels_gpu(
    const ProcessedGpuFramePtr& source, std::int64_t timestamp,
    const PrimaryWheelsParameters& parameters, ProcessedGpuFramePtr& output) {
    std::lock_guard lock(mutex_);
    output.reset();
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->process_primary_wheels_gpu(backend_->gpu_source(source),
                                                timestamp, parameters, output);
}

DigitorResult Engine::process_log_wheels_gpu(
    std::span<const Color> source, std::uint32_t width, std::uint32_t height,
    std::int64_t timestamp, const LogWheelsParameters& parameters,
    ProcessedGpuFramePtr& output) {
    std::lock_guard lock(mutex_);
    output.reset();
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->process_log_wheels_gpu(source, width, height, timestamp,
                                            parameters, output);
}

DigitorResult Engine::process_log_wheels_gpu(
    const ProcessedGpuFramePtr& source, std::int64_t timestamp,
    const LogWheelsParameters& parameters, ProcessedGpuFramePtr& output) {
    std::lock_guard lock(mutex_);
    output.reset();
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->process_log_wheels_gpu(backend_->gpu_source(source), timestamp,
                                            parameters, output);
}

DigitorResult Engine::validation_readback_primary_wheels(
    const ProcessedGpuFramePtr& frame, std::span<Color> output) {
    std::lock_guard lock(mutex_);
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->validation_readback_primary_wheels(frame, output);
}

DigitorResult Engine::validation_readback_log_wheels(
    const ProcessedGpuFramePtr& frame, std::span<Color> output) {
    std::lock_guard lock(mutex_);
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->validation_readback_log_wheels(frame, output);
}

DigitorResult Engine::validation_readback_final_frame(
    const ProcessedGpuFramePtr& frame, std::span<Color> output) {
    std::lock_guard lock(mutex_);
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->validation_readback_final_frame(frame, output);
}

DigitorResult Engine::present_gpu_frame(const ProcessedGpuFramePtr& frame) {
    std::scoped_lock lock(mutex_);
    if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    return backend_->present_gpu_frame(frame);
}

NativeNodeGraphResult Engine::execute_native_node_graph(
    const ProductionNodeGraph& graph, std::span<const Color> source,
    std::uint32_t width, std::uint32_t height, std::int64_t timestamp) {
    std::scoped_lock lock(mutex_);
    if (!initialized_ || !backend_) {
        return {NativeNodeGraphStatus::backend_failure,
                DIGITOR_RESULT_NOT_INITIALIZED,
                {}, {}, NodeOperationKind::primary_wheels,
                "engine is not initialized"};
    }
    return digitor::execute_native_node_graph(*backend_, graph, source, width,
                                               height, timestamp);
}

}  // namespace digitor
