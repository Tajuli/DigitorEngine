#include "digitor/primary_wheels.hpp"
#include "digitor/rgb_curves.hpp"
#include "gpu/gpu_backend.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>
namespace { class WheelsBackend final:public digitor::IRenderBackend{public:bool initialize(bool)override{return true;}void shutdown()noexcept override{}DigitorRendererInfo info()const noexcept override{return {};}protected:DigitorResult execute_process_primary_wheels_gpu(std::span<const digitor::Color>,uint32_t w,uint32_t h,int64_t ts,const digitor::PrimaryWheelsParameters&p,digitor::ProcessedGpuFramePtr&out)noexcept override{begin_grade_provenance(DIGITOR_RENDERER_VULKAN,true,"test-adapter","test","wheels","pipeline");provenance_.primary_wheels_enabled=true;provenance_.primary_wheels_parameter_identity=p.identity();provenance_.primary_wheels_source_bound=provenance_.primary_wheels_destination_bound=provenance_.primary_wheels_parameters_bound=provenance_.output_written=true;out=std::make_shared<digitor::ProcessedGpuFrame>(this,DIGITOR_RENDERER_VULKAN,digitor::GpuFrameMetadata{w,h,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,digitor::GpuFrameAlpha::straight,ts,"linear-rgba"},99,std::make_shared<int>(1),std::make_shared<std::atomic_bool>(true),true);return DIGITOR_RESULT_OK;}}; }
void test_primary_wheels(){using namespace digitor;
 reset_primary_wheels_reference_count();
 auto identity=PrimaryWheelsParameters::create();assert(identity->is_identity()&&identity->serialize()==identity->identity());
 Color a{-.25f,1.5f,.5f,.37f};auto b=apply_primary_wheels_reference(a,*identity);assert(b.r==a.r&&b.g==a.g&&b.b==a.b&&b.a==a.a);
 assert(primary_wheels_reference_count()==1);
 PrimaryWheelsDescriptor d;d.lift={.1f,0,0};d.gamma={2,1,1};d.gain={1,2,1};d.offset={0,0,.25f};auto p=PrimaryWheelsParameters::create(d);b=apply_primary_wheels_reference(a,*p);
 assert(std::abs(b.r+std::sqrt(.15f))<1e-6f&&b.g==3&&b.b==.75f&&b.a==a.a);
 d.lift_enabled=false;d.gamma_enabled=false;d.gain_enabled=false;d.offset_enabled=false;assert(PrimaryWheelsParameters::create(d)->is_identity());
 d= {};d.lift_master=.1f;d.gamma_master=2;d.gain_master=2;d.offset_master=-.1f;p=PrimaryWheelsParameters::create(d);b=apply_primary_wheels_reference({.15f,.15f,.15f,.2f},*p);assert(b.r==b.g&&b.g==b.b&&b.a==.2f);
 const float inf=std::numeric_limits<float>::infinity(),nan=std::numeric_limits<float>::quiet_NaN();b=apply_primary_wheels_reference({inf,nan,-2,.4f},*p);assert(b.r==inf&&std::isnan(b.g)&&std::isfinite(b.b)&&b.a==.4f);
 bool bad=false;try{d.gain.r=nan;(void)PrimaryWheelsParameters::create(d);}catch(const std::invalid_argument&){bad=true;}assert(bad);
 std::vector<Color> in(15,a),out(15);apply_primary_wheels_reference(in,out,*identity);for(auto v:out)assert(v.r==a.r&&v.g==a.g&&v.b==a.b&&v.a==a.a);
 RenderGraph graph;auto s=graph.import_resource({.size=in.size()*sizeof(Color),.transient=false,.initial_state=ResourceState::shader_read,.name="input"}),o=graph.create_transient(out.size()*sizeof(Color));add_primary_wheels_cpu_pass(graph,s,o,identity,in,out);graph.compile();CommandQueue q;graph.execute(q);assert(graph.order().size()==1);
 // User-defined graph ordering is supported and remains explicit: exercise
 // both Primary Wheels -> RGB Curves and the reverse comparison ordering.
 for(bool wheels_first:{false,true}){RenderGraph chain;auto input=chain.import_resource({.size=64,.transient=false,.initial_state=ResourceState::shader_read,.name="chain-input"});auto middle=chain.create_transient(64),output=chain.create_transient(64);int sequence=0;
   auto wheels=[&](CommandEncoder&e){e.dispatch([&]{sequence=sequence*10+1;});};auto curves=[&](CommandEncoder&e){e.dispatch([&]{sequence=sequence*10+2;});};
   if(wheels_first){add_primary_wheels_pass(chain,input,middle,wheels);add_rgb_curves_pass(chain,middle,output,curves);}else{add_rgb_curves_pass(chain,input,middle,curves);add_primary_wheels_pass(chain,middle,output,wheels);}chain.export_resource(output);chain.compile();CommandQueue cq;chain.execute(cq);assert(sequence==(wheels_first?12:21));assert(chain.schedule()[1].dependencies.size()==1);
 }
 WheelsBackend native;ProcessedGpuFramePtr frame;const auto before=primary_wheels_reference_count();assert(native.process_primary_wheels_gpu(in,3,5,91,*identity,frame)==DIGITOR_RESULT_OK);assert(frame&&frame->identity()==99&&frame->metadata().timestamp==91);const auto&prov=native.execution_provenance();assert(primary_wheels_reference_count()==before&&prov.cpu_primary_wheels_invocations==0&&prov.primary_wheels_fallback_invocations==0&&prov.normal_preview_readback_count==0);
}
