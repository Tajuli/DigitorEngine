#include "core/engine.hpp"

#include "cpu/cpu_backend.hpp"
#include "platform/platform.hpp"

namespace digitor {

Engine &Engine::instance() {
  static Engine engine;
  return engine;
}

DigitorResult Engine::initialize(const DigitorEngineConfig &config) {
  std::scoped_lock lock(mutex_);

  if (initialized_) {
    return DIGITOR_RESULT_ALREADY_INITIALIZED;
  }

  const bool valid_backend = config.preferred_backend == DIGITOR_RENDERER_AUTO ||
      config.preferred_backend == DIGITOR_RENDERER_VULKAN ||
      config.preferred_backend == DIGITOR_RENDERER_METAL ||
      config.preferred_backend == DIGITOR_RENDERER_D3D12 ||
      config.preferred_backend == DIGITOR_RENDERER_OPENGL_ES ||
      config.preferred_backend == DIGITOR_RENDERER_CPU;
  if (!valid_backend || config.enable_validation > 1 ||
      config.allow_cpu_fallback > 1)
    return DIGITOR_RESULT_INVALID_ARGUMENT;

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

  if (!contexts_.empty())
    return DIGITOR_RESULT_RESOURCE_IN_USE;

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

DigitorResult Engine::create_context(RenderContext **out_context) {
  if (out_context == nullptr) {
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  *out_context = nullptr;

  std::scoped_lock lock(mutex_);

  if (!initialized_ || !backend_) {
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }

  try {
    *out_context = new RenderContext(*backend_);
    contexts_.insert(*out_context);
  } catch (const std::bad_alloc &) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  }
  return DIGITOR_RESULT_OK;
}

DigitorResult Engine::destroy_context(RenderContext *context) {
  if (context == nullptr) {
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  std::scoped_lock lock(mutex_);
  if (!contexts_.contains(context))
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  if (context->has_resources()) {
    return DIGITOR_RESULT_RESOURCE_IN_USE;
  }
  contexts_.erase(context);
  delete context;
  return DIGITOR_RESULT_OK;
}

DigitorResult Engine::render_preview_rgba8(uint32_t width, uint32_t height,
                                           std::span<const uint8_t> source,
                                           std::vector<uint8_t> &destination) {
  std::scoped_lock lock(mutex_);
  if (!initialized_ || !backend_)
    return DIGITOR_RESULT_NOT_INITIALIZED;
  return backend_->render_rgba8(width, height, source, destination);
}

DigitorResult Engine::grade_rgba32f(std::span<const Color> source,
                                    std::span<Color> destination,
                                    const ColorGrade &parameters) {
  std::scoped_lock lock(mutex_);
  if (!initialized_ || !backend_)
    return DIGITOR_RESULT_NOT_INITIALIZED;
  return backend_->grade_rgba32f(source, destination, parameters);
}
DigitorResult Engine::curves_rgba32f(std::span<const Color> source,
                                     std::span<Color> destination,
                                     const CompiledRgbCurves& curves) {
  std::scoped_lock lock(mutex_);
  if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
  return backend_->curves_rgba32f(source,destination,curves);
}
DigitorResult Engine::process_curves_gpu(std::span<const Color> source,
    std::uint32_t width, std::uint32_t height, std::int64_t timestamp,
    const CompiledRgbCurves& curves, ProcessedGpuFramePtr& out) {
  std::scoped_lock lock(mutex_);
  out.reset();
  if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
  return backend_->process_curves_gpu(source,width,height,timestamp,curves,out);
}
DigitorResult Engine::process_primary_wheels_gpu(std::span<const Color>s,std::uint32_t w,std::uint32_t h,std::int64_t ts,const PrimaryWheelsParameters&p,ProcessedGpuFramePtr&out){std::lock_guard lock(mutex_);out.reset();if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->process_primary_wheels_gpu(s,w,h,ts,p,out);}
DigitorResult Engine::present_gpu_frame(const ProcessedGpuFramePtr& frame) {
  std::scoped_lock lock(mutex_);
  if (!initialized_ || !backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
  return backend_->present_gpu_frame(frame);
}

} // namespace digitor
