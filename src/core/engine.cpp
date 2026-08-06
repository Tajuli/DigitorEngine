#include "core/engine.hpp"

#include "cpu/cpu_backend.hpp"
#include "platform/platform.hpp"

namespace digitor {

Engine &Engine::instance() { static Engine engine; return engine; }

DigitorResult Engine::initialize(const DigitorEngineConfig &config) {
    std::scoped_lock lock(mutex_);
    if (initialized_) return DIGITOR_RESULT_ALREADY_INITIALIZED;
    const bool valid = config.preferred_backend == DIGITOR_RENDERER_AUTO ||
        config.preferred_backend == DIGITOR_RENDERER_VULKAN ||
        config.preferred_backend == DIGITOR_RENDERER_METAL ||
        config.preferred_backend == DIGITOR_RENDERER_D3D12 ||
        config.preferred_backend == DIGITOR_RENDERER_OPENGL_ES ||
        config.preferred_backend == DIGITOR_RENDERER_CPU;
    if (!valid || config.enable_validation > 1 || config.allow_cpu_fallback > 1)
        return DIGITOR_RESULT_INVALID_ARGUMENT;

    config_ = config;
    const bool explicit_cpu = config.preferred_backend == DIGITOR_RENDERER_CPU;
    const bool automatic = config.preferred_backend == DIGITOR_RENDERER_AUTO;

    // Preserve the existing public meaning of allow_cpu_fallback for explicit
    // CPU requests while enforcing GPU-first automatic selection.
    if (explicit_cpu && !config.allow_cpu_fallback)
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    backend_ = explicit_cpu ? std::make_unique<CpuBackend>()
                            : create_gpu_backend(config.preferred_backend);
    if (backend_ && backend_->initialize(config.enable_validation != 0)) {
        initialized_ = true;
        return DIGITOR_RESULT_OK;
    }
    if (backend_) {
        backend_->shutdown();
        backend_.reset();
    }

    // CPU is selected only when AUTO cannot establish any usable GPU backend.
    // Explicit GPU requests remain strict. Once selected, the backend is locked
    // for the engine lifetime and failures never switch execution mid-session.
    if (automatic && config.allow_cpu_fallback) {
        auto cpu = std::make_unique<CpuBackend>();
        if (!cpu->initialize(config.enable_validation != 0))
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        backend_ = std::move(cpu);
        initialized_ = true;
        return DIGITOR_RESULT_OK;
    }
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
}

DigitorResult Engine::shutdown(){std::scoped_lock lock(mutex_);if(!initialized_)return DIGITOR_RESULT_NOT_INITIALIZED;if(!contexts_.empty())return DIGITOR_RESULT_RESOURCE_IN_USE;if(backend_){backend_->shutdown();backend_.reset();}initialized_=false;return DIGITOR_RESULT_OK;}
bool Engine::is_initialized()const noexcept{std::scoped_lock lock(mutex_);return initialized_;}
DigitorRendererInfo Engine::renderer_info()const noexcept{std::scoped_lock lock(mutex_);return backend_?backend_->info():DigitorRendererInfo{};}
DigitorResult Engine::create_context(RenderContext**out){if(!out)return DIGITOR_RESULT_INVALID_ARGUMENT;*out=nullptr;std::scoped_lock lock(mutex_);if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;try{*out=new RenderContext(*backend_);contexts_.insert(*out);}catch(const std::bad_alloc&){return DIGITOR_RESULT_OUT_OF_MEMORY;}return DIGITOR_RESULT_OK;}
DigitorResult Engine::destroy_context(RenderContext*c){if(!c)return DIGITOR_RESULT_INVALID_ARGUMENT;std::scoped_lock lock(mutex_);if(!contexts_.contains(c))return DIGITOR_RESULT_INVALID_ARGUMENT;if(c->has_resources())return DIGITOR_RESULT_RESOURCE_IN_USE;contexts_.erase(c);delete c;return DIGITOR_RESULT_OK;}
DigitorResult Engine::render_preview_rgba8(uint32_t w,uint32_t h,std::span<const uint8_t>s,std::vector<uint8_t>&d){std::scoped_lock lock(mutex_);if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->render_rgba8(w,h,s,d);}
DigitorResult Engine::grade_rgba32f(std::span<const Color>s,std::span<Color>d,const ColorGrade&p){std::scoped_lock lock(mutex_);if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->grade_rgba32f(s,d,p);}
DigitorResult Engine::curves_rgba32f(std::span<const Color>s,std::span<Color>d,const CompiledRgbCurves&c){std::scoped_lock lock(mutex_);if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->curves_rgba32f(s,d,c);}
DigitorResult Engine::process_curves_gpu(std::span<const Color>s,std::uint32_t w,std::uint32_t h,std::int64_t ts,const CompiledRgbCurves&c,ProcessedGpuFramePtr&o){std::scoped_lock lock(mutex_);o.reset();if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->process_curves_gpu(s,w,h,ts,c,o);}
DigitorResult Engine::process_primary_wheels_gpu(std::span<const Color>s,std::uint32_t w,std::uint32_t h,std::int64_t ts,const PrimaryWheelsParameters&p,ProcessedGpuFramePtr&o){std::lock_guard lock(mutex_);o.reset();if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->process_primary_wheels_gpu(s,w,h,ts,p,o);}
DigitorResult Engine::process_curves_gpu(const ProcessedGpuFramePtr&s,std::int64_t ts,const CompiledRgbCurves&c,ProcessedGpuFramePtr&o){std::lock_guard lock(mutex_);o.reset();if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->process_curves_gpu(backend_->gpu_source(s),ts,c,o);}
DigitorResult Engine::process_primary_wheels_gpu(const ProcessedGpuFramePtr&s,std::int64_t ts,const PrimaryWheelsParameters&p,ProcessedGpuFramePtr&o){std::lock_guard lock(mutex_);o.reset();if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->process_primary_wheels_gpu(backend_->gpu_source(s),ts,p,o);}
DigitorResult Engine::process_log_wheels_gpu(std::span<const Color>s,std::uint32_t w,std::uint32_t h,std::int64_t ts,const LogWheelsParameters&p,ProcessedGpuFramePtr&o){std::lock_guard lock(mutex_);o.reset();if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->process_log_wheels_gpu(s,w,h,ts,p,o);}
DigitorResult Engine::process_log_wheels_gpu(const ProcessedGpuFramePtr&s,std::int64_t ts,const LogWheelsParameters&p,ProcessedGpuFramePtr&o){std::lock_guard lock(mutex_);o.reset();if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->process_log_wheels_gpu(backend_->gpu_source(s),ts,p,o);}
DigitorResult Engine::validation_readback_primary_wheels(const ProcessedGpuFramePtr&f,std::span<Color>o){std::lock_guard lock(mutex_);if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->validation_readback_primary_wheels(f,o);}
DigitorResult Engine::validation_readback_log_wheels(const ProcessedGpuFramePtr&f,std::span<Color>o){std::lock_guard lock(mutex_);if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->validation_readback_log_wheels(f,o);}
DigitorResult Engine::validation_readback_final_frame(const ProcessedGpuFramePtr&f,std::span<Color>o){std::lock_guard lock(mutex_);if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->validation_readback_final_frame(f,o);}
DigitorResult Engine::present_gpu_frame(const ProcessedGpuFramePtr&f){std::scoped_lock lock(mutex_);if(!initialized_||!backend_)return DIGITOR_RESULT_NOT_INITIALIZED;return backend_->present_gpu_frame(f);}
NativeNodeGraphResult Engine::execute_native_node_graph(const ProductionNodeGraph& graph,std::span<const Color> source,std::uint32_t width,std::uint32_t height,std::int64_t timestamp){std::scoped_lock lock(mutex_);if(!initialized_||!backend_)return {NativeNodeGraphStatus::backend_failure,DIGITOR_RESULT_NOT_INITIALIZED,{},{},NodeOperationKind::primary_wheels,"engine is not initialized"};return digitor::execute_native_node_graph(*backend_,graph,source,width,height,timestamp);}

} // namespace digitor
