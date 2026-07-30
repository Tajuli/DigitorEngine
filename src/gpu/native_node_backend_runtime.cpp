#include "digitor/native_node_backend_runtime.hpp"
#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace digitor {

bool NativeNodeShaderBinary::valid_for(const NativeNodeCompiledPipeline& p) const noexcept {
  if (!p.ready || bytes.empty() || contract_hash != p.contract_hash) return false;
  switch (p.backend) {
    case DIGITOR_RENDERER_VULKAN: return format == NativeNodeBinaryFormat::spirv && (bytes.size()%4u)==0;
    case DIGITOR_RENDERER_D3D12: return format == NativeNodeBinaryFormat::dxil;
    case DIGITOR_RENDERER_METAL: return format == NativeNodeBinaryFormat::metallib;
    case DIGITOR_RENDERER_OPENGL_ES: return format == NativeNodeBinaryFormat::glsl_es;
    default: return false;
  }
}

bool validate_native_node_dispatch_resources(const NativeNodePipelineContract& c,
                                             const NativeNodeDispatchResources& r,
                                             std::string& d) noexcept {
  if (!validate_native_node_pipeline_contract(c)) { d="invalid pipeline contract"; return false; }
  if (r.constants.size() != c.constant_bytes) { d="constant byte count mismatch"; return false; }
  std::unordered_set<std::uint32_t> seen;
  std::uint32_t expected_texture_count = 0;
  for (std::uint32_t i=0;i<c.binding_count;++i) {
    const auto& expected=c.bindings[i];
    if (expected.kind == NativeNodeBindingKind::constants) continue;
    ++expected_texture_count;
    auto it=std::find_if(r.textures.begin(),r.textures.end(),[&](const auto& t){return t.slot==expected.binding;});
    if (it==r.textures.end() || it->native_texture==0 || it->width==0 || it->height==0) {
      d="missing or invalid texture binding at slot "+std::to_string(expected.binding); return false;
    }
    if (!seen.insert(it->slot).second) { d="duplicate texture binding"; return false; }
  }
  if (r.textures.size()!=expected_texture_count) { d="unexpected texture binding count"; return false; }
  d.clear(); return true;
}

struct NativeNodeBackendRuntime::Entry {
  NativeNodeCompiledPipeline compiled;
  NativeNodeShaderBinary binary;
  NativeNodeBackendPipelineHandle handle;
  std::uint64_t device{};
};

std::uint64_t NativeNodeBackendRuntime::key(DigitorRendererBackend b, NativeNodeKernel k,
                                            std::uint64_t h, std::uint64_t d) noexcept {
  std::uint64_t x=1469598103934665603ull;
  auto mix=[&](std::uint64_t v){for(int i=0;i<8;++i){x^=(v>>(i*8))&0xffu;x*=1099511628211ull;}};
  mix(static_cast<std::uint64_t>(b));mix(static_cast<std::uint64_t>(k));mix(h);mix(d);return x;
}

NativeNodeBackendRuntime::NativeNodeBackendRuntime(NativeNodeCompileBinaryFn c,
 NativeNodeCreateBackendPipelineFn p, NativeNodeDestroyBackendPipelineFn d,
 NativeNodeRecordBackendDispatchFn r):compile_(std::move(c)),create_(std::move(p)),destroy_(std::move(d)),record_(std::move(r)){}
NativeNodeBackendRuntime::~NativeNodeBackendRuntime(){clear();}

bool NativeNodeBackendRuntime::prepare(DigitorRendererBackend b,NativeNodeKernel k,
 std::uint32_t w,std::uint32_t h,std::uint64_t device,std::string& diag) noexcept {
 try {
  if(!compile_||!create_||device==0){diag="backend runtime callbacks/device unavailable";return false;}
  auto p=prepare_native_node_pipeline(b,k,w,h); if(!p.ready){diag=p.diagnostic;return false;}
  auto id=key(b,k,p.contract_hash,device);
  {std::lock_guard l(mutex_);if(entries_.contains(id)){diag.clear();return true;}}
  NativeNodeShaderBinary bin; if(!compile_(p,bin,diag)||!bin.valid_for(p)){if(diag.empty())diag="invalid compiled shader binary";return false;}
  NativeNodeBackendPipelineHandle handle; if(!create_(p,bin,device,handle,diag)||handle.pipeline==0){if(diag.empty())diag="native pipeline creation failed";return false;}
  auto e=std::make_shared<Entry>();e->compiled=std::move(p);e->binary=std::move(bin);e->handle=handle;e->device=device;
  std::lock_guard l(mutex_);auto [it,inserted]=entries_.emplace(id,e);if(!inserted&&destroy_)destroy_(handle);diag.clear();return true;
 }catch(...){diag="native backend pipeline preparation threw";return false;}
}

bool NativeNodeBackendRuntime::dispatch(DigitorRendererBackend b,NativeNodeKernel k,
 std::uint32_t w,std::uint32_t h,std::uint64_t device,const NativeNodeDispatchResources&r,
 std::string&diag) noexcept {
 if(r.kernel!=k){diag="native dispatch kernel identity mismatch";return false;}
 if(!prepare(b,k,w,h,device,diag))return false;
 auto p=prepare_native_node_pipeline(b,k,w,h);auto id=key(b,k,p.contract_hash,device);std::shared_ptr<Entry>e;
 {std::lock_guard l(mutex_);auto it=entries_.find(id);if(it==entries_.end()){diag="prepared pipeline disappeared";return false;}e=it->second;}
 auto contract=native_node_pipeline_contract(b,k);if(!validate_native_node_dispatch_resources(contract,r,diag))return false;
 try{return record_&&record_(e->handle,p.geometry,r,diag);}catch(...){diag="native dispatch recording threw";return false;}
}

void NativeNodeBackendRuntime::retire_device(std::uint64_t d) noexcept {std::lock_guard l(mutex_);for(auto it=entries_.begin();it!=entries_.end();){if(it->second->device==d){if(destroy_)destroy_(it->second->handle);it=entries_.erase(it);}else ++it;}}
void NativeNodeBackendRuntime::clear() noexcept {std::lock_guard l(mutex_);for(auto&[_,e]:entries_)if(destroy_)destroy_(e->handle);entries_.clear();}
std::size_t NativeNodeBackendRuntime::size()const noexcept{std::lock_guard l(mutex_);return entries_.size();}

} // namespace digitor
