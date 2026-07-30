#import <Metal/Metal.h>
#include "digitor/native_node_platform_factories.hpp"

namespace digitor {
bool create_metal_native_node_pipeline(const NativeNodePlatformFactoryContext& ctx,
 const NativeNodeCompiledPipeline& compiled,const NativeNodeShaderBinary& binary,
 NativeNodeBackendPipelineHandle& out,std::string& diagnostic) noexcept {
 out={}; @autoreleasepool {
  if(compiled.backend!=DIGITOR_RENDERER_METAL||binary.format!=NativeNodeBinaryFormat::metallib||
     !binary.valid_for(compiled)||!ctx.device||binary.bytes.empty()) {diagnostic="invalid Metal native-node pipeline input";return false;}
  id<MTLDevice> device=(__bridge id<MTLDevice>)reinterpret_cast<void*>(ctx.device);
  dispatch_data_t data=dispatch_data_create(binary.bytes.data(),binary.bytes.size(),dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  NSError* error=nil; id<MTLLibrary> library=[device newLibraryWithData:data error:&error];
  if(!library){diagnostic="newLibraryWithData failed";return false;}
  auto contract=native_node_pipeline_contract(compiled.backend,compiled.kernel);
  NSString* name=[[NSString alloc] initWithBytes:contract.entry_point.data() length:contract.entry_point.size() encoding:NSUTF8StringEncoding];
  id<MTLFunction> function=[library newFunctionWithName:name];
  if(!function){diagnostic="Metal entry point not found";return false;}
  id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:function error:&error];
  if(!pipeline){diagnostic="newComputePipelineStateWithFunction failed";return false;}
  out.pipeline=reinterpret_cast<std::uintptr_t>((__bridge_retained void*)pipeline);
  diagnostic.clear(); return true;
 }
}
void destroy_metal_native_node_pipeline(const NativeNodePlatformFactoryContext&,const NativeNodeBackendPipelineHandle& h) noexcept {
 if(h.pipeline) CFRelease(reinterpret_cast<CFTypeRef>(h.pipeline));
}
bool record_metal_native_node_dispatch(const NativeNodePlatformFactoryContext& ctx,const NativeNodeBackendPipelineHandle& h,
 const NativeNodeDispatchGeometry& geometry,const NativeNodeDispatchResources& resources,std::string& diagnostic) noexcept {
 @autoreleasepool {
  if(!ctx.command_context||!h.pipeline||geometry.groups_x==0||geometry.groups_y==0||geometry.groups_z==0){diagnostic="invalid Metal node dispatch context";return false;}
  id<MTLComputeCommandEncoder> encoder=(__bridge id<MTLComputeCommandEncoder>)reinterpret_cast<void*>(ctx.command_context);
  id<MTLComputePipelineState> pipeline=(__bridge id<MTLComputePipelineState>)reinterpret_cast<void*>(h.pipeline);
  [encoder setComputePipelineState:pipeline];
  for(const auto& t:resources.textures){ if(!t.native_texture){diagnostic="invalid Metal texture";return false;} id<MTLTexture> texture=(__bridge id<MTLTexture>)reinterpret_cast<void*>(t.native_texture); [encoder setTexture:texture atIndex:t.slot]; }
  if(!resources.constants.empty()) [encoder setBytes:resources.constants.data() length:resources.constants.size() atIndex:0];
  MTLSize threads=MTLSizeMake(8,8,1); MTLSize groups=MTLSizeMake(geometry.groups_x,geometry.groups_y,geometry.groups_z);
  [encoder dispatchThreadgroups:groups threadsPerThreadgroup:threads]; diagnostic.clear(); return true;
 }
}
} // namespace digitor
