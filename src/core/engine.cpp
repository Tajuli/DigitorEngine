#include "core/engine.hpp"
#include "core/engine_production_runtime.hpp"

#include "cpu/cpu_backend.hpp"
#include "digitor/native_node_mask_backend.hpp"
#include "platform/platform.hpp"

#include <exception>
#include <unordered_map>
#include <vector>

namespace digitor {
namespace {

bool production_mask_operation(NodeOperationKind kind) noexcept {
    return kind == NodeOperationKind::hsl_qualifier ||
           kind == NodeOperationKind::power_window;
}

NativeNodeGraphResult production_graph_failure(
    NativeNodeGraphStatus status, DigitorResult backend_result, NodeId node,
    NodeOperationKind operation, std::string message) {
    NativeNodeGraphResult result{};
    result.status = status;
    result.backend_result = backend_result;
    result.node = node;
    result.operation = operation;
    result.message = std::move(message);
    return result;
}

} // namespace

Engine &Engine::instance() { static Engine engine; return engine; }

DigitorResult Engine::finish_backend_initialization_locked() {
    if (!backend_) return DIGITOR_RESULT_NOT_INITIALIZED;
    const auto capability = backend_->production_capability();
    if (capability.valid() && engine_production_runtime_supported_platform()) {
        std::string diagnostic;
        production_runtime_ = install_engine_production_runtime(capability, &diagnostic);
        if (!production_runtime_) {
            backend_->shutdown();
            backend_.reset();
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        }
    }
    initialized_ = true;
    return DIGITOR_RESULT_OK;
}

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
        return finish_backend_initialization_locked();
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
        return finish_backend_initialization_locked();
    }
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
}

DigitorResult Engine::shutdown(){std::scoped_lock lock(mutex_);if(!initialized_)return DIGITOR_RESULT_NOT_INITIALIZED;if(!contexts_.empty())return DIGITOR_RESULT_RESOURCE_IN_USE;if(production_runtime_){const auto r=production_runtime_->shutdown();if(r!=DIGITOR_RESULT_OK)return r;production_runtime_.reset();}if(backend_){backend_->shutdown();backend_.reset();}initialized_=false;return DIGITOR_RESULT_OK;}
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

NativeNodeGraphResult execute_native_node_graph(
    IRenderBackend& backend, const ProductionNodeGraph& graph,
    const ProcessedGpuFramePtr& source, std::int64_t timestamp) noexcept {
    const auto preflight = preflight_native_node_graph(backend, graph);
    if (!preflight.supported) {
        return production_graph_failure(
            preflight.message.find("mixer") != std::string::npos
                ? NativeNodeGraphStatus::unsupported_parallel_mixer
                : NativeNodeGraphStatus::unsupported_operation,
            DIGITOR_RESULT_UNSUPPORTED, preflight.node, preflight.operation,
            preflight.message);
    }

    try {
        if (!source || !source->ready() || !source->context_live() ||
            source->backend() == DIGITOR_RENDERER_CPU) {
            return production_graph_failure(
                NativeNodeGraphStatus::backend_failure,
                DIGITOR_RESULT_BACKEND_UNAVAILABLE, graph.input_node(),
                NodeOperationKind::primary_wheels,
                "production graph requires a live, ready GPU-resident input frame");
        }
        const auto initial = backend.gpu_source(source);
        if (initial.readiness != GpuReadiness::Ready ||
            initial.backend != backend.info().backend) {
            return production_graph_failure(
                NativeNodeGraphStatus::backend_failure,
                DIGITOR_RESULT_BACKEND_UNAVAILABLE, graph.input_node(),
                NodeOperationKind::primary_wheels,
                "decoded GPU frame does not belong to the selected renderer backend/context");
        }

        const auto width = source->metadata().width;
        const auto height = source->metadata().height;
        if (width == 0 || height == 0) {
            return production_graph_failure(
                NativeNodeGraphStatus::backend_failure,
                DIGITOR_RESULT_INVALID_ARGUMENT, graph.input_node(),
                NodeOperationKind::primary_wheels,
                "decoded GPU frame has invalid dimensions");
        }

        std::unordered_map<NodeId, ProcessedGpuFramePtr> outputs;
        outputs.emplace(graph.input_node(), source);
        ProcessedGpuFramePtr final_frame;

        auto process_color_operation = [&](ProcessedGpuFramePtr& current,
                                           NodeId node,
                                           const NodeOperation& operation)
            -> NativeNodeGraphResult {
            if (!current) {
                return production_graph_failure(
                    NativeNodeGraphStatus::backend_failure,
                    DIGITOR_RESULT_INTERNAL_ERROR, node, operation.kind,
                    "native graph operation received no GPU input frame");
            }
            const auto gpu = backend.gpu_source(current);
            if (gpu.readiness != GpuReadiness::Ready) {
                return production_graph_failure(
                    NativeNodeGraphStatus::backend_failure,
                    DIGITOR_RESULT_RESOURCE_IN_USE, node, operation.kind,
                    "native graph operation received an unready GPU input frame");
            }

            ProcessedGpuFramePtr next;
            DigitorResult backend_result = DIGITOR_RESULT_UNSUPPORTED;
            switch (operation.kind) {
                case NodeOperationKind::primary_wheels: {
                    const auto parameters = std::get<std::shared_ptr<const PrimaryWheelsParameters>>(operation.payload);
                    if (!parameters) return production_graph_failure(NativeNodeGraphStatus::backend_failure,DIGITOR_RESULT_INVALID_ARGUMENT,node,operation.kind,"primary wheels operation has no parameters");
                    backend_result = backend.process_primary_wheels_gpu(gpu, timestamp, *parameters, next);
                    break;
                }
                case NodeOperationKind::log_wheels: {
                    const auto parameters = std::get<std::shared_ptr<const LogWheelsParameters>>(operation.payload);
                    if (!parameters) return production_graph_failure(NativeNodeGraphStatus::backend_failure,DIGITOR_RESULT_INVALID_ARGUMENT,node,operation.kind,"log wheels operation has no parameters");
                    backend_result = backend.process_log_wheels_gpu(gpu, timestamp, *parameters, next);
                    break;
                }
                case NodeOperationKind::rgb_curves: {
                    const auto parameters = std::get<std::shared_ptr<const CompiledRgbCurves>>(operation.payload);
                    if (!parameters) return production_graph_failure(NativeNodeGraphStatus::backend_failure,DIGITOR_RESULT_INVALID_ARGUMENT,node,operation.kind,"RGB curves operation has no compiled parameters");
                    backend_result = backend.process_curves_gpu(gpu, timestamp, *parameters, next);
                    break;
                }
                case NodeOperationKind::hsl_qualifier:
                case NodeOperationKind::power_window:
                    return production_graph_failure(
                        NativeNodeGraphStatus::backend_failure,
                        DIGITOR_RESULT_INTERNAL_ERROR, node, operation.kind,
                        "mask operation entered the color-operation executor");
                default:
                    backend_result = backend.process_node_operation_gpu(
                        gpu, timestamp, operation, next);
                    break;
            }
            if (backend_result != DIGITOR_RESULT_OK || !next) {
                return production_graph_failure(
                    backend_result == DIGITOR_RESULT_UNSUPPORTED
                        ? NativeNodeGraphStatus::unsupported_operation
                        : NativeNodeGraphStatus::backend_failure,
                    backend_result, node, operation.kind,
                    backend_result == DIGITOR_RESULT_UNSUPPORTED
                        ? "backend does not implement selected-node native pass"
                        : "backend-native node pass failed");
            }
            current = std::move(next);
            NativeNodeGraphResult ok{};
            ok.status = NativeNodeGraphStatus::ok;
            ok.backend_result = DIGITOR_RESULT_OK;
            return ok;
        };

        for (const auto id : graph.execution_order()) {
            const auto& node = graph.node(id);
            if (node.kind == ProductionNodeKind::input) continue;

            if (node.kind == ProductionNodeKind::output) {
                if (node.inputs.size() != 1 || !outputs.contains(node.inputs.front())) {
                    return production_graph_failure(
                        NativeNodeGraphStatus::invalid_graph,
                        DIGITOR_RESULT_INVALID_ARGUMENT, id,
                        NodeOperationKind::primary_wheels,
                        "output node has no native GPU input");
                }
                final_frame = outputs.at(node.inputs.front());
                break;
            }

            if (node.kind == ProductionNodeKind::mixer) {
                std::vector<GpuSourceResource> sources;
                sources.reserve(node.inputs.size());
                for (const auto input : node.inputs) {
                    const auto found = outputs.find(input);
                    if (found == outputs.end() || !found->second) {
                        return production_graph_failure(
                            NativeNodeGraphStatus::invalid_graph,
                            DIGITOR_RESULT_INVALID_ARGUMENT, id,
                            NodeOperationKind::primary_wheels,
                            "parallel mixer input is unavailable");
                    }
                    auto gpu = backend.gpu_source(found->second);
                    if (gpu.readiness != GpuReadiness::Ready) {
                        return production_graph_failure(
                            NativeNodeGraphStatus::backend_failure,
                            DIGITOR_RESULT_RESOURCE_IN_USE, id,
                            NodeOperationKind::primary_wheels,
                            "parallel mixer received an unready GPU source");
                    }
                    sources.push_back(std::move(gpu));
                }
                ProcessedGpuFramePtr mixed;
                const auto backend_result = backend.mix_gpu_sources(sources, timestamp, mixed);
                if (backend_result != DIGITOR_RESULT_OK || !mixed) {
                    return production_graph_failure(
                        backend_result == DIGITOR_RESULT_UNSUPPORTED
                            ? NativeNodeGraphStatus::unsupported_parallel_mixer
                            : NativeNodeGraphStatus::backend_failure,
                        backend_result, id, NodeOperationKind::primary_wheels,
                        "backend-native parallel mixer failed");
                }
                outputs[id] = std::move(mixed);
                continue;
            }

            if (node.inputs.size() != 1 || !outputs.contains(node.inputs.front())) {
                return production_graph_failure(
                    NativeNodeGraphStatus::invalid_graph,
                    DIGITOR_RESULT_INVALID_ARGUMENT, id,
                    NodeOperationKind::primary_wheels,
                    "grade node must have exactly one available GPU input");
            }

            auto original = outputs.at(node.inputs.front());
            if (!node.enabled || node.bypassed || node.operations.empty()) {
                outputs[id] = std::move(original);
                continue;
            }

            std::vector<const NodeOperation*> masks;
            for (const auto& operation : node.operations) {
                if (operation.enabled && production_mask_operation(operation.kind)) {
                    masks.push_back(&operation);
                }
            }

            auto current = original;
            for (const auto& operation : node.operations) {
                if (!operation.enabled || production_mask_operation(operation.kind)) continue;
                const auto operation_result = process_color_operation(current, id, operation);
                if (operation_result.backend_result != DIGITOR_RESULT_OK) {
                    return operation_result;
                }
            }

            if (!masks.empty()) {
                auto* mask_backend = dynamic_cast<NativeNodeMaskBackend*>(&backend);
                if (!mask_backend) {
                    return production_graph_failure(
                        NativeNodeGraphStatus::unsupported_operation,
                        DIGITOR_RESULT_UNSUPPORTED, id, masks.front()->kind,
                        "selected renderer does not expose native node-mask execution");
                }
                const auto original_source = backend.gpu_source(original);
                const auto processed_source = backend.gpu_source(current);
                if (original_source.readiness != GpuReadiness::Ready ||
                    processed_source.readiness != GpuReadiness::Ready) {
                    return production_graph_failure(
                        NativeNodeGraphStatus::backend_failure,
                        DIGITOR_RESULT_RESOURCE_IN_USE, id, masks.front()->kind,
                        "masked node received an unready GPU frame");
                }

                std::vector<GpuMatteResourcePtr> mattes;
                mattes.reserve(masks.size());
                for (const auto* operation : masks) {
                    GpuMatteResourcePtr matte;
                    DigitorResult backend_result = DIGITOR_RESULT_UNSUPPORTED;
                    if (operation->kind == NodeOperationKind::hsl_qualifier) {
                        const auto parameters = std::get<std::shared_ptr<const HslQualifierParameters>>(operation->payload);
                        if (!parameters) {
                            return production_graph_failure(
                                NativeNodeGraphStatus::backend_failure,
                                DIGITOR_RESULT_INVALID_ARGUMENT, id, operation->kind,
                                "HSL qualifier mask has no parameters");
                        }
                        backend_result = mask_backend->generate_hsl_matte(
                            original_source, timestamp, *parameters, matte);
                    } else {
                        backend_result = mask_backend->generate_power_window_matte(
                            width, height, timestamp,
                            std::get<PowerWindowSettings>(operation->payload), matte);
                    }
                    if (backend_result != DIGITOR_RESULT_OK || !matte ||
                        !matte->usable_by(original_source.backend,
                                         original_source.context_identity)) {
                        return production_graph_failure(
                            backend_result == DIGITOR_RESULT_UNSUPPORTED
                                ? NativeNodeGraphStatus::unsupported_operation
                                : NativeNodeGraphStatus::backend_failure,
                            backend_result, id, operation->kind,
                            "backend-native matte generation failed");
                    }
                    const auto& metadata = matte->metadata();
                    if (metadata.width != width || metadata.height != height ||
                        metadata.format != GpuMatteFormat::r32_float) {
                        return production_graph_failure(
                            NativeNodeGraphStatus::backend_failure,
                            DIGITOR_RESULT_INTERNAL_ERROR, id, operation->kind,
                            "backend returned an invalid node-local matte resource");
                    }
                    mattes.push_back(std::move(matte));
                }

                GpuMatteResourcePtr combined = mattes.front();
                if (mattes.size() > 1) {
                    combined.reset();
                    const auto backend_result = mask_backend->multiply_mattes(
                        mattes, timestamp, combined);
                    if (backend_result != DIGITOR_RESULT_OK || !combined ||
                        !combined->usable_by(original_source.backend,
                                             original_source.context_identity)) {
                        return production_graph_failure(
                            backend_result == DIGITOR_RESULT_UNSUPPORTED
                                ? NativeNodeGraphStatus::unsupported_operation
                                : NativeNodeGraphStatus::backend_failure,
                            backend_result, id, masks.front()->kind,
                            "backend-native matte multiplication failed");
                    }
                }

                ProcessedGpuFramePtr composited;
                const auto backend_result = mask_backend->composite_with_matte(
                    original_source, processed_source, combined, timestamp,
                    composited);
                if (backend_result != DIGITOR_RESULT_OK || !composited) {
                    return production_graph_failure(
                        backend_result == DIGITOR_RESULT_UNSUPPORTED
                            ? NativeNodeGraphStatus::unsupported_operation
                            : NativeNodeGraphStatus::backend_failure,
                        backend_result, id, masks.front()->kind,
                        "backend-native masked composite failed");
                }
                current = std::move(composited);
            }

            outputs[id] = std::move(current);
        }

        if (!final_frame) {
            return production_graph_failure(
                NativeNodeGraphStatus::backend_failure,
                DIGITOR_RESULT_INTERNAL_ERROR, graph.output_node(),
                NodeOperationKind::primary_wheels,
                "production graph produced no final GPU frame");
        }
        NativeNodeGraphResult result{};
        result.status = NativeNodeGraphStatus::ok;
        result.backend_result = DIGITOR_RESULT_OK;
        result.frame = std::move(final_frame);
        result.message = "decoded GPU frame completed the production node graph without CPU readback";
        return result;
    } catch (const std::bad_alloc&) {
        return production_graph_failure(
            NativeNodeGraphStatus::backend_failure,
            DIGITOR_RESULT_OUT_OF_MEMORY, 0,
            NodeOperationKind::primary_wheels,
            "production GPU graph allocation failed");
    } catch (const std::exception& error) {
        return production_graph_failure(
            NativeNodeGraphStatus::backend_failure,
            DIGITOR_RESULT_INTERNAL_ERROR, 0,
            NodeOperationKind::primary_wheels, error.what());
    } catch (...) {
        return production_graph_failure(
            NativeNodeGraphStatus::backend_failure,
            DIGITOR_RESULT_INTERNAL_ERROR, 0,
            NodeOperationKind::primary_wheels,
            "unknown production GPU graph failure");
    }
}

NativeNodeGraphResult Engine::execute_native_node_graph(
    const ProductionNodeGraph& graph, const ProcessedGpuFramePtr& source,
    std::int64_t timestamp) {
    std::scoped_lock lock(mutex_);
    if (!initialized_ || !backend_) {
        return production_graph_failure(
            NativeNodeGraphStatus::backend_failure,
            DIGITOR_RESULT_NOT_INITIALIZED, 0,
            NodeOperationKind::primary_wheels, "engine is not initialized");
    }
    return digitor::execute_native_node_graph(*backend_, graph, source, timestamp);
}

} // namespace digitor
