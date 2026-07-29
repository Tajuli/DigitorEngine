#include "digitor/log_wheels.hpp"
#include "gpu/native_log_wheels.hpp"
#include "gpu/gpu_backend.hpp"
#include <atomic>
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>


namespace {
class LogBackend final : public digitor::IRenderBackend {
 public:
  bool initialize(bool) override { return true; }
  void shutdown() noexcept override {}
  DigitorRendererInfo info() const noexcept override { DigitorRendererInfo i{}; i.backend=DIGITOR_RENDERER_VULKAN; i.is_gpu=true; return i; }
 protected:
  digitor::ProcessedGpuFramePtr make(uint32_t w,uint32_t h,int64_t ts){
    return std::make_shared<digitor::ProcessedGpuFrame>(this,DIGITOR_RENDERER_VULKAN,digitor::GpuFrameMetadata{w,h,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,digitor::GpuFrameAlpha::straight,ts,"linear-rgba"},700,std::make_shared<int>(1),std::make_shared<std::atomic_bool>(true),true);
  }
  DigitorResult execute_process_log_wheels_gpu(std::span<const digitor::Color>,uint32_t w,uint32_t h,int64_t ts,const digitor::LogWheelsParameters&p,digitor::ProcessedGpuFramePtr&o) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_VULKAN,true,"fixture","fixture-compiler","log-wheels-fixture","log-wheels-pipeline");
    provenance_.log_wheels_enabled=true; provenance_.log_wheels_parameter_identity=p.identity(); provenance_.log_wheels_source_bound=true; provenance_.log_wheels_destination_bound=true; provenance_.log_wheels_parameters_bound=true; provenance_.output_written=true; o=make(w,h,ts); return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_process_log_wheels_gpu(const digitor::GpuSourceResource&s,int64_t ts,const digitor::LogWheelsParameters&p,digitor::ProcessedGpuFramePtr&o) noexcept override {
    return execute_process_log_wheels_gpu({},s.width,s.height,ts,p,o);
  }
  DigitorResult execute_validation_readback_log_wheels(const digitor::ProcessedGpuFramePtr&f,std::span<digitor::Color>o) noexcept override {
    if(!f||o.size()!=size_t(f->metadata().width)*f->metadata().height) return DIGITOR_RESULT_INVALID_ARGUMENT;
    for(auto& c:o)c={0,0,0,1}; provenance_.readback_performed=true; return DIGITOR_RESULT_OK;
  }
};
}

void test_log_wheels(){using namespace digitor;
 reset_log_wheels_reference_count();
 auto identity=LogWheelsParameters::create();assert(identity->is_identity());
 Color input{.2f,.4f,.8f,.37f};auto output=apply_log_wheels_reference(input,*identity);
 assert(output.r==input.r&&output.g==input.g&&output.b==input.b&&output.a==input.a);
 assert(log_wheels_reference_count()==1);
 LogWheelsDescriptor d;d.global.master=1.0f;auto global=LogWheelsParameters::create(d);
 output=apply_log_wheels_reference(input,*global);assert(std::abs(output.r-.4f)<1e-6f&&std::abs(output.g-.8f)<1e-6f&&std::abs(output.b-1.6f)<1e-6f&&output.a==input.a);
 d={};d.shadows.rgb={.1f,0,0};d.midtones.rgb={0,.1f,0};d.highlights.rgb={0,0,.1f};auto bands=LogWheelsParameters::create(d);
 auto dark=apply_log_wheels_reference({0,0,0,.2f},*bands);auto mid=apply_log_wheels_reference({.5f,.5f,.5f,.3f},*bands);auto bright=apply_log_wheels_reference({1,1,1,.4f},*bands);
 assert(dark.r>.09f&&dark.g<.01f&&dark.b<.01f);assert(mid.g>.09f&&mid.r<.51f&&mid.b<.51f);assert(bright.b>1.09f&&bright.r<1.01f&&bright.g<1.01f);
 auto native=native_log_wheels_parameters(*bands,15,3,5);assert(native.shadows[0]==.1f&&native.midtones[1]==.1f&&native.highlights[2]==.1f&&native.pixel_count==15&&native.width==3&&native.height==5);
 d={};d.shadow_pivot=.8f;d.highlight_pivot=.2f;bool bad=false;try{(void)LogWheelsParameters::create(d);}catch(const std::invalid_argument&){bad=true;}assert(bad);
 d={};d.global.master=std::numeric_limits<float>::quiet_NaN();bad=false;try{(void)LogWheelsParameters::create(d);}catch(const std::invalid_argument&){bad=true;}assert(bad);
 std::vector<Color> in(12,input),out(12);apply_log_wheels_reference(in,out,*identity);for(auto c:out)assert(c.r==input.r&&c.a==input.a);
 RenderGraph graph;auto source=graph.import_resource({.size=in.size()*sizeof(Color),.transient=false,.initial_state=ResourceState::shader_read,.name="log-input"});auto destination=graph.create_transient(out.size()*sizeof(Color));add_log_wheels_cpu_pass(graph,source,destination,identity,in,out);graph.export_resource(destination);graph.compile();CommandQueue queue;graph.execute(queue);assert(graph.order().size()==1);
 int executed=0;RenderGraph native_graph;auto ns=native_graph.import_resource({.size=64,.transient=false,.initial_state=ResourceState::shader_read,.name="native-log-input"});auto nd=native_graph.create_transient(64);add_log_wheels_pass(native_graph,ns,nd,[&](CommandEncoder&e){e.dispatch([&]{++executed;});});native_graph.export_resource(nd);native_graph.compile();CommandQueue nq;native_graph.execute(nq);assert(executed==1);
}
