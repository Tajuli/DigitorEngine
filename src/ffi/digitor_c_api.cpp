#include "digitor/digitor.h"

#include <mutex>
#include <new>
#include <unordered_set>

#include "core/engine.hpp"
#include "core/resources.hpp"
#include "digitor/production_node_graph.hpp"
#include <cstring>
#include <string>

struct DigitorRenderContext { digitor::RenderContext* impl; };
struct DigitorTexture { digitor::Texture* impl; };
struct DigitorBuffer { digitor::Buffer* impl; };
struct DigitorSampler { digitor::Sampler* impl; };
struct DigitorNodeGraph { digitor::ProductionNodeGraph impl; };

namespace {
std::recursive_mutex g_handles_mutex;
std::unordered_set<void*> g_contexts;
std::unordered_set<void*> g_textures;
std::unordered_set<void*> g_buffers;
std::unordered_set<void*> g_samplers;
std::unordered_set<void*> g_node_graphs;

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


DigitorResult digitor_node_graph_create(DigitorNodeGraph** out_graph) {
    if (!out_graph) return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out_graph = nullptr;
    return c_api_guard([&]{ std::scoped_lock lock(g_handles_mutex); auto* g=new(std::nothrow) DigitorNodeGraph{}; if(!g)return DIGITOR_RESULT_OUT_OF_MEMORY; g_node_graphs.insert(g); *out_graph=g; return DIGITOR_RESULT_OK; });
}
DigitorResult digitor_node_graph_destroy(DigitorNodeGraph* graph) { return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(!contains(g_node_graphs,graph))return DIGITOR_RESULT_INVALID_ARGUMENT;g_node_graphs.erase(graph);delete graph;return DIGITOR_RESULT_OK;}); }
static DigitorResult valid_graph(DigitorNodeGraph* g){return contains(g_node_graphs,g)?DIGITOR_RESULT_OK:DIGITOR_RESULT_INVALID_ARGUMENT;}
DigitorResult digitor_node_graph_get_endpoints(DigitorNodeGraph* g,DigitorNodeId* a,DigitorNodeId* b){if(!a||!b)return DIGITOR_RESULT_INVALID_ARGUMENT;*a=*b=0;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;*a=g->impl.input_node();*b=g->impl.output_node();return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_select(DigitorNodeGraph* g,DigitorNodeId n){return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;g->impl.select_node(n);return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_get_selected(DigitorNodeGraph* g,DigitorNodeId* n){if(!n)return DIGITOR_RESULT_INVALID_ARGUMENT;*n=0;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;*n=g->impl.selected_node();return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_add_serial_after(DigitorNodeGraph* g,DigitorNodeId a,const char* name,DigitorNodeId* out){if(!out)return DIGITOR_RESULT_INVALID_ARGUMENT;*out=0;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;*out=g->impl.add_serial_after(a,name?name:"Serial Node");return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_add_parallel_after(DigitorNodeGraph* g,DigitorNodeId a,const char* x,const char* y,DigitorNodeId* ox,DigitorNodeId* oy){if(!ox||!oy)return DIGITOR_RESULT_INVALID_ARGUMENT;*ox=*oy=0;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;auto p=g->impl.add_parallel_after(a,x?x:"Parallel A",y?y:"Parallel B");*ox=p.first;*oy=p.second;return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_convert_to_parallel(DigitorNodeGraph* g,DigitorNodeId n,const char* name,DigitorNodeId* out){if(!out)return DIGITOR_RESULT_INVALID_ARGUMENT;*out=0;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;*out=g->impl.convert_to_parallel(n,name?name:"Parallel Node");return DIGITOR_RESULT_OK;});}
#define NODE_CALL(name,expr) DigitorResult name(DigitorNodeGraph* g,DigitorNodeId a){return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;expr;return DIGITOR_RESULT_OK;});}
NODE_CALL(digitor_node_graph_remove,g->impl.remove_node(a))
NODE_CALL(digitor_node_graph_clear_operations,g->impl.clear_operations(a))
#undef NODE_CALL
DigitorResult digitor_node_graph_connect(DigitorNodeGraph*g,DigitorNodeId a,DigitorNodeId b){return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;g->impl.connect(a,b);return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_disconnect(DigitorNodeGraph*g,DigitorNodeId a,DigitorNodeId b){return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;g->impl.disconnect(a,b);return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_set_position(DigitorNodeGraph*g,DigitorNodeId n,DigitorNodePosition p){return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;g->impl.set_position(n,{p.x,p.y});return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_set_enabled(DigitorNodeGraph*g,DigitorNodeId n,uint8_t v){return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;g->impl.set_enabled(n,v!=0);return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_set_bypassed(DigitorNodeGraph*g,DigitorNodeId n,uint8_t v){return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;g->impl.set_bypassed(n,v!=0);return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_add_primary_wheels(DigitorNodeGraph*g,const DigitorPrimaryWheelsControls*c){if(!c)return DIGITOR_RESULT_INVALID_ARGUMENT;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;digitor::PrimaryWheelsDescriptor d;auto rgb=[](DigitorRgb v){return digitor::PrimaryRgb{v.r,v.g,v.b};};d.lift=rgb(c->lift);d.lift_master=c->lift_master;d.lift_enabled=c->lift_enabled;d.gamma=rgb(c->gamma);d.gamma_master=c->gamma_master;d.gamma_enabled=c->gamma_enabled;d.gain=rgb(c->gain);d.gain_master=c->gain_master;d.gain_enabled=c->gain_enabled;d.offset=rgb(c->offset);d.offset_master=c->offset_master;d.offset_enabled=c->offset_enabled;g->impl.add_operation_to_selected(digitor::make_primary_wheels_operation(digitor::PrimaryWheelsParameters::create(d)));return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_add_log_wheels(DigitorNodeGraph*g,const DigitorLogWheelsControls*c){if(!c)return DIGITOR_RESULT_INVALID_ARGUMENT;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;digitor::LogWheelsDescriptor d;auto cv=[](DigitorLogWheelControl v){digitor::LogWheelControl o;o.rgb={v.rgb.r,v.rgb.g,v.rgb.b};o.master=v.master;o.enabled=v.enabled;return o;};d.shadows=cv(c->shadows);d.midtones=cv(c->midtones);d.highlights=cv(c->highlights);d.global=cv(c->global);d.shadow_pivot=c->shadow_pivot;d.highlight_pivot=c->highlight_pivot;d.transition_width=c->transition_width;g->impl.add_operation_to_selected(digitor::make_log_wheels_operation(digitor::LogWheelsParameters::create(d)));return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_add_rgb_curves(DigitorNodeGraph*g,const DigitorRgbCurvesControls*c){if(!c)return DIGITOR_RESULT_INVALID_ARGUMENT;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;digitor::RgbCurvesParameters p;p.lut_size=c->lut_size?c->lut_size:1024;auto cv=[](const DigitorCurveChannel&in,digitor::RgbCurveDefinition&out){if(!in.points||in.point_count<2||in.point_count>digitor::rgb_curve_max_points)throw std::invalid_argument("curve points");out.enabled=in.enabled!=0;out.points.clear();out.points.reserve(in.point_count);for(uint32_t i=0;i<in.point_count;++i)out.points.push_back({in.points[i].x,in.points[i].y});};cv(c->master,p.master);cv(c->red,p.red);cv(c->green,p.green);cv(c->blue,p.blue);g->impl.add_operation_to_selected(digitor::make_rgb_curves_operation(digitor::CompiledRgbCurves::compile(p)));return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_add_hsl_qualifier(DigitorNodeGraph*g,const DigitorHslQualifierControls*c){if(!c)return DIGITOR_RESULT_INVALID_ARGUMENT;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;digitor::QualifierSettings q;auto rg=[](DigitorQualifierRange r){return digitor::QualifierRange{r.low,r.high,r.softness};};q.hue=rg(c->hue);q.saturation=rg(c->saturation);q.luminance=rg(c->luminance);q.blur=c->blur;q.denoise=c->denoise;q.clean_black=c->clean_black;q.clean_white=c->clean_white;q.invert=c->invert!=0;q.matte_output=c->matte_output!=0;g->impl.add_operation_to_selected(digitor::make_hsl_qualifier_operation(digitor::HslQualifierParameters::create(q)));return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_add_lut1d(DigitorNodeGraph*g,const DigitorLut1DControls*c){if(!c||!c->values||c->value_count<2)return DIGITOR_RESULT_INVALID_ARGUMENT;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK||c->interpolation>DIGITOR_LUT_TETRAHEDRAL)return DIGITOR_RESULT_INVALID_ARGUMENT;std::vector<digitor::Color>v;v.reserve(c->value_count);for(uint32_t i=0;i<c->value_count;++i)v.push_back({c->values[i].r,c->values[i].g,c->values[i].b,c->values[i].a});auto lut=std::make_shared<const digitor::Lut1D>(std::move(v));g->impl.add_operation_to_selected(digitor::make_lut_operation(lut,static_cast<digitor::LutInterpolation>(c->interpolation)));return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_add_lut3d(DigitorNodeGraph*g,const DigitorLut3DControls*c){if(!c||!c->values||c->size<2)return DIGITOR_RESULT_INVALID_ARGUMENT;const uint64_t expected=uint64_t(c->size)*c->size*c->size;if(c->value_count!=expected)return DIGITOR_RESULT_INVALID_ARGUMENT;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK||c->interpolation>DIGITOR_LUT_TETRAHEDRAL)return DIGITOR_RESULT_INVALID_ARGUMENT;std::vector<digitor::Color>v;v.reserve((size_t)c->value_count);for(uint64_t i=0;i<c->value_count;++i)v.push_back({c->values[i].r,c->values[i].g,c->values[i].b,c->values[i].a});auto lut=std::make_shared<const digitor::Lut3D>(c->size,std::move(v));g->impl.add_operation_to_selected(digitor::make_lut_operation(lut,static_cast<digitor::LutInterpolation>(c->interpolation)));return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_add_effect(DigitorNodeGraph*g,const DigitorNodeEffectSettings*c){if(!c)return DIGITOR_RESULT_INVALID_ARGUMENT;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK||c->type>DIGITOR_NODE_EFFECT_MOTION_BLUR)return DIGITOR_RESULT_INVALID_ARGUMENT;digitor::EffectSettings e{static_cast<digitor::EffectType>(c->type),c->amount,c->radius,c->angle,c->seed};g->impl.add_operation_to_selected(digitor::make_effect_operation(e));return DIGITOR_RESULT_OK;});}
DigitorResult digitor_node_graph_add_power_window(DigitorNodeGraph*g,const DigitorPowerWindowSettings*c){if(!c)return DIGITOR_RESULT_INVALID_ARGUMENT;return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK||c->shape>DIGITOR_WINDOW_LINEAR_GRADIENT)return DIGITOR_RESULT_INVALID_ARGUMENT;digitor::PowerWindowSettings w{static_cast<digitor::WindowShape>(c->shape),c->center_x,c->center_y,c->width,c->height,c->rotation,c->feather,c->opacity,c->invert!=0};g->impl.add_operation_to_selected(digitor::make_power_window_operation(w));return DIGITOR_RESULT_OK;});}
static DigitorResult copy_text(const std::string&s,char*b,uint64_t z,uint64_t*req){if(!req)return DIGITOR_RESULT_INVALID_ARGUMENT;*req=s.size()+1;if(!b)return DIGITOR_RESULT_OK;if(z<*req)return DIGITOR_RESULT_INVALID_ARGUMENT;std::memcpy(b,s.c_str(),*req);return DIGITOR_RESULT_OK;}
DigitorResult digitor_node_graph_recipe_identity(DigitorNodeGraph*g,char*b,uint64_t z,uint64_t*r){return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;return copy_text(g->impl.recipe_identity(),b,z,r);});}
DigitorResult digitor_node_graph_to_json(DigitorNodeGraph*g,char*b,uint64_t z,uint64_t*r){return c_api_guard([&]{std::scoped_lock lock(g_handles_mutex);if(valid_graph(g)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;return copy_text(g->impl.to_json(),b,z,r);});}

} // extern "C"
