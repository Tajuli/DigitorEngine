#include "digitor/native_node_hardware_harness.hpp"
#include "digitor/native_node_pipeline_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace digitor {
namespace {
std::vector<Color> make_a(std::size_t n){std::vector<Color> v(n);for(std::size_t i=0;i<n;++i){float t=float(i%17)/16.f;v[i]={t,1.f-t,.25f+.5f*t,1.f};}return v;}
std::vector<Color> make_b(std::size_t n){std::vector<Color> v(n);for(std::size_t i=0;i<n;++i){float t=float(i%13)/12.f;v[i]={1.f-t,.2f+.6f*t,t,1.f};}return v;}
std::vector<float> make_m(std::size_t n){std::vector<float> v(n);for(std::size_t i=0;i<n;++i)v[i]=float(i%11)/10.f;return v;}
double max_error(std::span<const Color>a,std::span<const Color>b){double m=0;for(std::size_t i=0;i<a.size();++i){m=std::max(m,std::abs(double(a[i].r)-b[i].r));m=std::max(m,std::abs(double(a[i].g)-b[i].g));m=std::max(m,std::abs(double(a[i].b)-b[i].b));m=std::max(m,std::abs(double(a[i].a)-b[i].a));}return m;}
}

NativeNodeHardwareHarnessResult run_native_node_hardware_qualification(
    const NativeNodeHardwareHarnessIdentity& id,
    const NativeNodeHardwareHarnessCallbacks& cb,
    NativeNodeKernel kernel,std::uint32_t width,std::uint32_t height,double tolerance) noexcept {
  NativeNodeHardwareHarnessResult r;
  auto& e=r.evidence;e.backend=id.backend;e.kernel=kernel;e.contract_hash=native_node_pipeline_contract_hash(id.backend,kernel);e.device_identity=id.device_identity;e.platform=id.platform;e.device_name=id.device_name;e.driver_version=id.driver_version;
  std::ostringstream eid;eid<<id.evidence_id_prefix<<'-'<<native_node_backend_name(id.backend)<<'-'<<native_node_kernel_name(kernel)<<'-'<<id.device_identity;e.evidence_id=eid.str();
  if(id.backend==DIGITOR_RENDERER_CPU||!id.device_identity||!width||!height){r.diagnostic="invalid hardware harness identity or dimensions";return r;}
  if(!cb.prepare||!cb.execute_and_readback||!cb.pipeline_create_count||!cb.pipeline_cache_hit_count||!cb.intermediate_cpu_readback_count||!cb.retire_device||!cb.verify_retired){r.diagnostic="incomplete hardware harness callbacks";return r;}
  try{
    std::string d;auto creates0=cb.pipeline_create_count();auto hits0=cb.pipeline_cache_hit_count();auto reads0=cb.intermediate_cpu_readback_count();
    if(!cb.prepare(kernel,width,height,d)){r.diagnostic="pipeline preparation failed: "+d;return r;}e.pipeline_created=cb.pipeline_create_count()>creates0;
    if(!cb.prepare(kernel,width,height,d)){r.diagnostic="pipeline cache reuse preparation failed: "+d;return r;}e.pipeline_cache_reused=cb.pipeline_cache_hit_count()>hits0;
    const std::size_t n=std::size_t(width)*height; auto a=make_a(n); auto b=make_b(n); auto m=make_m(n); std::vector<Color> gpu(n),ref(n);
    if(kernel==NativeNodeKernel::parallel_mixer){std::span<const Color> ins_arr[2]={a,b};float weights[2]={.25f,.75f};node_mixer_reference(std::span<const std::span<const Color>>(ins_arr,2),std::span<const float>(weights,2),ref);}
    else masked_composite_reference(a,b,m,ref);
    e.dispatch_recorded=cb.execute_and_readback(kernel,width,height,a,b,m,gpu,d);
    if(!e.dispatch_recorded){r.diagnostic="native dispatch/readback failed: "+d;return r;}e.gpu_completed=true;e.max_abs_error=max_error(ref,gpu);e.cpu_gpu_parity=std::isfinite(e.max_abs_error)&&e.max_abs_error<=tolerance;e.no_cpu_readback=cb.intermediate_cpu_readback_count()==reads0;
    cb.retire_device(id.device_identity);e.retirement_invalidated=cb.verify_retired(kernel,d);if(!e.retirement_invalidated&&r.diagnostic.empty())r.diagnostic="device retirement verification failed: "+d;
    std::string vdiag;r.passed=validate_native_node_hardware_evidence(e,tolerance,vdiag);if(!r.passed&&r.diagnostic.empty())r.diagnostic=vdiag;
  }catch(const std::exception& ex){r.diagnostic=ex.what();}catch(...){r.diagnostic="hardware qualification harness threw";}
  return r;
}
} // namespace digitor
