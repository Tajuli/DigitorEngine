#include "digitor/native_node_production_signoff.hpp"
#include "digitor/native_node_shader_contracts.hpp"
#include <cmath>
#include <map>
#include <mutex>
#include <sstream>

namespace digitor {
namespace {
using OpKey=std::pair<int,int>;
struct OpRecord { NativeNodeSignoffState state{NativeNodeSignoffState::missing}; std::uint64_t hash{}; std::string evidence; };
struct KernelRecord { NativeNodeSignoffState state{NativeNodeSignoffState::missing}; std::uint64_t hash{}; std::string evidence; };
std::string esc(const std::string&s){std::string o;for(char c:s){if(c=='"'||c=='\\')o.push_back('\\');o.push_back(c);}return o;}
}

const char* native_node_operation_name(NodeOperationKind k) noexcept {
  switch(k){
    case NodeOperationKind::primary_wheels:return "primary_wheels";
    case NodeOperationKind::log_wheels:return "log_wheels";
    case NodeOperationKind::rgb_curves:return "rgb_curves";
    case NodeOperationKind::hsl_qualifier:return "hsl_qualifier";
    case NodeOperationKind::correction:return "correction";
    case NodeOperationKind::lut1d:return "lut1d";
    case NodeOperationKind::lut3d:return "lut3d";
    case NodeOperationKind::effect:return "effect";
    case NodeOperationKind::power_window:return "power_window";
  }
  return "unknown";
}

bool validate_native_node_operation_evidence(const NativeNodeOperationEvidence& e,double tol,std::string& d) noexcept {
  d.clear();
  if(e.backend==DIGITOR_RENDERER_CPU){d="CPU is not a native GPU backend";return false;}
  if(!e.implementation_hash){d="missing implementation hash";return false;}
  if(!e.device_identity||e.platform.empty()||e.device_name.empty()||e.driver_version.empty()||e.evidence_id.empty()){d="missing device or evidence identity";return false;}
  if(!e.native_pipeline_created){d="native pipeline was not created";return false;}
  if(!e.native_dispatch_completed){d="native dispatch did not complete";return false;}
  if(!e.cpu_gpu_parity){d="CPU/GPU parity failed";return false;}
  if(!e.no_intermediate_readback){d="intermediate CPU readback detected";return false;}
  if(!e.cache_reused){d="pipeline cache reuse not proven";return false;}
  if(!e.retirement_invalidated){d="device retirement invalidation not proven";return false;}
  if(!std::isfinite(e.max_abs_error)||e.max_abs_error>tol){d="maximum absolute error exceeds tolerance";return false;}
  return true;
}

struct NativeNodeProductionSignoff::Impl {
  mutable std::mutex mutex;
  std::map<OpKey,OpRecord> ops;
  std::map<OpKey,KernelRecord> kernels;
};
NativeNodeProductionSignoff::NativeNodeProductionSignoff():impl_(new Impl){}
NativeNodeProductionSignoff::~NativeNodeProductionSignoff(){delete impl_;}

void NativeNodeProductionSignoff::mark_implemented(DigitorRendererBackend b,NodeOperationKind o,std::uint64_t h){
  std::lock_guard lock(impl_->mutex); auto& r=impl_->ops[{(int)b,(int)o}]; r.state=h?NativeNodeSignoffState::implemented_unqualified:NativeNodeSignoffState::missing;r.hash=h;r.evidence.clear();
}

bool NativeNodeProductionSignoff::record_evidence(const NativeNodeOperationEvidence& e,double tol,std::string& d){
  if(!validate_native_node_operation_evidence(e,tol,d))return false;
  std::lock_guard lock(impl_->mutex);auto it=impl_->ops.find({(int)e.backend,(int)e.operation});
  if(it==impl_->ops.end()||it->second.hash!=e.implementation_hash){d="implementation hash mismatch or operation not marked implemented";return false;}
  it->second.state=NativeNodeSignoffState::qualified;it->second.evidence=e.evidence_id;return true;
}

void NativeNodeProductionSignoff::record_kernel_evidence(const NativeNodeHardwareQualificationEvidence& e,double tol){
  std::string d;if(!validate_native_node_hardware_evidence(e,tol,d))return;
  std::lock_guard lock(impl_->mutex);auto& r=impl_->kernels[{(int)e.backend,(int)e.kernel}];r.state=NativeNodeSignoffState::qualified;r.hash=e.contract_hash;r.evidence=e.evidence_id;
}

NativeNodeBackendSignoffReport NativeNodeProductionSignoff::report(DigitorRendererBackend b,const std::vector<NodeOperationKind>& req,std::uint64_t mixh,std::uint64_t maskh)const{
  NativeNodeBackendSignoffReport out;out.backend=b;out.production_ready=true;std::lock_guard lock(impl_->mutex);
  for(auto op:req){NativeNodeOperationSignoff s;s.backend=b;s.operation=op;auto it=impl_->ops.find({(int)b,(int)op});if(it!=impl_->ops.end()){s.state=it->second.state;s.implementation_hash=it->second.hash;}if(s.state!=NativeNodeSignoffState::qualified){out.production_ready=false;s.diagnostic=s.state==NativeNodeSignoffState::missing?"missing":"implemented but hardware-unqualified";}out.operations.push_back(std::move(s));}
  auto fill_kernel=[&](NativeNodeKernel k,std::uint64_t expected,NativeNodeOperationSignoff& s){s.backend=b;s.operation=k==NativeNodeKernel::parallel_mixer?NodeOperationKind::effect:NodeOperationKind::power_window;auto it=impl_->kernels.find({(int)b,(int)k});if(it!=impl_->kernels.end()){s.state=it->second.state;s.implementation_hash=it->second.hash;}if(s.state!=NativeNodeSignoffState::qualified||s.implementation_hash!=expected){out.production_ready=false;s.diagnostic=s.state==NativeNodeSignoffState::missing?"missing kernel evidence":"kernel evidence missing or contract hash mismatch";}};
  fill_kernel(NativeNodeKernel::parallel_mixer,mixh,out.parallel_mixer);fill_kernel(NativeNodeKernel::masked_composite,maskh,out.masked_composition);
  if(!out.production_ready) {
    out.diagnostic="native node backend is not production-signed-off";
  }
  return out;
}

void NativeNodeProductionSignoff::retire_backend(DigitorRendererBackend b) noexcept {std::lock_guard lock(impl_->mutex);for(auto it=impl_->ops.begin();it!=impl_->ops.end();)it=it->first.first==(int)b?impl_->ops.erase(it):std::next(it);for(auto it=impl_->kernels.begin();it!=impl_->kernels.end();)it=it->first.first==(int)b?impl_->kernels.erase(it):std::next(it);}

std::string NativeNodeProductionSignoff::report_json(const NativeNodeBackendSignoffReport& r)const{
  std::ostringstream o;o<<"{\"backend\":\""<<native_node_backend_name(r.backend)<<"\",\"production_ready\":"<<(r.production_ready?"true":"false")<<",\"operations\":[";for(std::size_t i=0;i<r.operations.size();++i){auto&s=r.operations[i];if(i)o<<',';o<<"{\"name\":\""<<native_node_operation_name(s.operation)<<"\",\"state\":"<<(int)s.state<<",\"hash\":"<<s.implementation_hash<<",\"diagnostic\":\""<<esc(s.diagnostic)<<"\"}";}o<<"],\"parallel_mixer_state\":"<<(int)r.parallel_mixer.state<<",\"masked_composition_state\":"<<(int)r.masked_composition.state<<",\"diagnostic\":\""<<esc(r.diagnostic)<<"\"}";return o.str();
}

} // namespace digitor
