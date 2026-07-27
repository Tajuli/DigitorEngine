#include "digitor/rgb_curves.hpp"
#include "gpu/native_rgb_curves.hpp"
#include "gpu/execution_provenance.hpp"
#include "digitor/gpu_frame.hpp"
#include "core/numeric_utils.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

void test_rgb_curves(){using namespace digitor;
 const auto counter_before=cpu_curve_reference_count();
 auto ready=std::make_shared<std::atomic_bool>(false);int context_a=0,context_b=0;
 auto native_owner=std::shared_ptr<void>(new int(7),[](void*p){delete static_cast<int*>(p);});
 ProcessedGpuFrame resource(&context_a,DIGITOR_RENDERER_VULKAN,
   {.width=3,.height=5,.timestamp=17},42,native_owner,ready,true);
 assert(resource.acquire(&context_a,DIGITOR_RENDERER_VULKAN)==DIGITOR_RESULT_RESOURCE_IN_USE);
 ready->store(true);assert(resource.acquire(&context_b,DIGITOR_RENDERER_VULKAN)==DIGITOR_RESULT_INVALID_ARGUMENT);
 assert(resource.acquire(&context_a,DIGITOR_RENDERER_D3D12)==DIGITOR_RESULT_INVALID_ARGUMENT);
 assert(resource.acquire(&context_a,DIGITOR_RENDERER_VULKAN)==DIGITOR_RESULT_OK);
 assert(resource.release(&context_a)==DIGITOR_RESULT_OK);
 assert(resource.release(&context_a)==DIGITOR_RESULT_INVALID_ARGUMENT);
 std::uint32_t checked=0;assert(checked_size_to_uint32(42,checked)&&checked==42);
 if constexpr(sizeof(std::size_t)>sizeof(std::uint32_t))assert(!checked_size_to_uint32(std::size_t{std::numeric_limits<std::uint32_t>::max()}+1,checked));
 RgbCurvesParameters p;auto identity=CompiledRgbCurves::compile(p);Color a{-.25f,1.5f,.375f,.37f};auto b=identity->apply(a);assert(b.r==a.r&&b.g==a.g&&b.b==a.b&&b.a==a.a);assert(cpu_curve_reference_count()==counter_before+1);
 p.red.points={{0,0},{.25f,.1f},{.5f,.8f},{1,1}};auto curve=CompiledRgbCurves::compile(p);auto hit=CompiledRgbCurves::compile(p);assert(curve==hit&&curve->lut_size()==1024&&curve->identity()==curve->serialize());b=curve->apply(a);assert(b.g==a.g&&b.b==a.b&&b.a==a.a);
 p.lut_size=256;assert(CompiledRgbCurves::compile(p)!=curve);p.lut_size=4096;assert(CompiledRgbCurves::compile(p)->curves()[1].samples.size()==4096);
 RgbCurvesParameters flat;flat.master.points={{0,0},{.3f,.2f},{.7f,.2f},{1,1}};auto f=CompiledRgbCurves::compile(flat);float last=-1;for(int n=0;n<=100;n++){float v=f->apply({n/100.f,0,0,1}).r;assert(v+1e-6f>=last);last=v;}
 RgbCurvesParameters ext;ext.master.points={{0,.1f},{1,.9f}};auto e=CompiledRgbCurves::compile(ext);b=e->apply({-.5f,1.5f,.5f,.2f});assert(b.r<.1f&&b.g>.9f&&b.a==.2f);
 bool bad=false;try{RgbCurvesParameters q;q.red.points={{0,0},{0,1}};(void)CompiledRgbCurves::compile(q);}catch(const std::invalid_argument&){bad=true;}assert(bad);
 bad=false;try{RgbCurvesParameters q;q.blue.points[0].x=std::numeric_limits<float>::quiet_NaN();(void)CompiledRgbCurves::compile(q);}catch(const std::invalid_argument&){bad=true;}assert(bad);
 bad=false;try{RgbCurvesParameters q;q.master.domain_min=2;(void)CompiledRgbCurves::compile(q);}catch(const std::invalid_argument&){bad=true;}assert(bad);
 std::vector<Color> in(15,a),out(15);curve->apply(in,out);for(auto q:out)assert(q.a==a.a);
 NativeRgbCurvesKey nk{curve->identity(),"device-a",RgbCurvesBackend::vulkan,RgbCurvesPrecision::fp32,curve->lut_size(),1,1};
 NativeRgbCurvesKey other{curve->identity(),"device-b",RgbCurvesBackend::vulkan,RgbCurvesPrecision::fp32,curve->lut_size(),1,1};assert(nk.serialize()!=other.serialize());
 NativeRgbCurvesCache native(2);int uploads=0;auto make=[&]{++uploads;auto r=std::make_shared<NativeRgbCurvesResource>();r->identity=nk.serialize();r->uploaded_bytes=curve->lut_size()*4*sizeof(float);return r;};
 auto cold=native.get_or_create(nk,make),warm=native.get_or_create(nk,make);assert(!cold.hit&&warm.hit&&uploads==1&&native.size()==1);
}
