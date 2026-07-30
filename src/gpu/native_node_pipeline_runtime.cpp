#include "digitor/native_node_pipeline_runtime.hpp"
#include <algorithm>
namespace digitor {
namespace {
std::uint64_t fnv(std::uint64_t h, const void* p, std::size_t n) noexcept {
 const auto* b=static_cast<const unsigned char*>(p);for(std::size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ull;}return h;
}
std::uint64_t fnv_sv(std::uint64_t h,std::string_view s) noexcept{return fnv(h,s.data(),s.size());}
}
std::uint64_t native_node_pipeline_contract_hash(DigitorRendererBackend b,NativeNodeKernel k) noexcept {
 auto c=native_node_pipeline_contract(b,k);if(!validate_native_node_pipeline_contract(c))return 0;
 std::uint64_t h=1469598103934665603ull;h=fnv(h,&b,sizeof b);h=fnv(h,&k,sizeof k);h=fnv_sv(h,c.entry_point);h=fnv_sv(h,c.source);h=fnv(h,&c.constant_bytes,sizeof c.constant_bytes);h=fnv(h,&c.local_size_x,sizeof c.local_size_x);h=fnv(h,&c.local_size_y,sizeof c.local_size_y);h=fnv(h,&c.local_size_z,sizeof c.local_size_z);h=fnv(h,&c.uses_push_constants,sizeof c.uses_push_constants);
 for(std::uint32_t i=0;i<c.binding_count;++i){h=fnv(h,&c.bindings[i].binding,sizeof c.bindings[i].binding);h=fnv(h,&c.bindings[i].kind,sizeof c.bindings[i].kind);h=fnv_sv(h,c.bindings[i].format);}return h;
}
NativeNodeDispatchGeometry native_node_dispatch_geometry(const NativeNodePipelineContract& c,std::uint32_t w,std::uint32_t h) noexcept {
 if(!validate_native_node_pipeline_contract(c)||w==0||h==0)return {};
 auto ceildiv=[](std::uint32_t a,std::uint32_t b){return (a+b-1)/b;};return {ceildiv(w,c.local_size_x),ceildiv(h,c.local_size_y),1};
}
NativeNodeCompiledPipeline prepare_native_node_pipeline(DigitorRendererBackend b,NativeNodeKernel k,std::uint32_t w,std::uint32_t h) noexcept {
 NativeNodeCompiledPipeline out;out.backend=b;out.kernel=k;auto c=native_node_pipeline_contract(b,k);out.contract_hash=native_node_pipeline_contract_hash(b,k);out.geometry=native_node_dispatch_geometry(c,w,h);
 if(!validate_native_node_pipeline_contract(c)){out.diagnostic="invalid backend shader contract";return out;}
 if(w==0||h==0){out.diagnostic="zero-sized dispatch target";return out;}
 // This prepares the backend-independent runtime record. Native backends must
 // only mark support after they have created their API pipeline object from
 // this exact contract hash and verified resource bindings.
 out.ready=true;out.diagnostic="runtime contract prepared; native API object creation required by backend";return out;
}
}
