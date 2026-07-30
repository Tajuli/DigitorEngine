#include "digitor/native_node_shader_contracts.hpp"
namespace digitor {
namespace {
constexpr NativeNodeBinding kMixerBindings[]={{0,NativeNodeBindingKind::sampled_or_storage_input,"rgba32f"},{1,NativeNodeBindingKind::sampled_or_storage_input,"rgba32f"},{2,NativeNodeBindingKind::storage_output,"rgba32f"},{3,NativeNodeBindingKind::constants,"16-bytes"}};
constexpr NativeNodeBinding kMaskBindings[]={{0,NativeNodeBindingKind::sampled_or_storage_input,"rgba32f"},{1,NativeNodeBindingKind::sampled_or_storage_input,"rgba32f"},{2,NativeNodeBindingKind::sampled_or_storage_input,"r32f"},{3,NativeNodeBindingKind::storage_output,"rgba32f"},{4,NativeNodeBindingKind::constants,"8-bytes"}};
constexpr std::string_view kMixerHlsl=R"(Texture2D<float4> InputA:register(t0);Texture2D<float4> InputB:register(t1);RWTexture2D<float4> Output:register(u0);cbuffer Params:register(b0){float weightA;float weightB;uint width;uint height;}[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(id.x>=width||id.y>=height)return;float a=max(weightA,0.0),b=max(weightB,0.0),s=max(a+b,1e-20);Output[id.xy]=(InputA[id.xy]*a+InputB[id.xy]*b)/s;})";
constexpr std::string_view kMaskHlsl=R"(Texture2D<float4> Original:register(t0);Texture2D<float4> Processed:register(t1);Texture2D<float> Matte:register(t2);RWTexture2D<float4> Output:register(u0);cbuffer Params:register(b0){uint width;uint height;}[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(id.x>=width||id.y>=height)return;float m=saturate(Matte[id.xy]);Output[id.xy]=lerp(Original[id.xy],Processed[id.xy],m);})";
constexpr std::string_view kMixerVk=R"(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;layout(set=0,binding=0,rgba32f)uniform readonly image2D inputA;layout(set=0,binding=1,rgba32f)uniform readonly image2D inputB;layout(set=0,binding=2,rgba32f)uniform writeonly image2D outputImage;layout(push_constant)uniform P{float weightA;float weightB;uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(id.x>=p.width||id.y>=p.height)return;float a=max(p.weightA,0.0),b=max(p.weightB,0.0),s=max(a+b,1e-20);imageStore(outputImage,ivec2(id),(imageLoad(inputA,ivec2(id))*a+imageLoad(inputB,ivec2(id))*b)/s);})";
constexpr std::string_view kMaskVk=R"(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;layout(set=0,binding=0,rgba32f)uniform readonly image2D originalImage;layout(set=0,binding=1,rgba32f)uniform readonly image2D processedImage;layout(set=0,binding=2,r32f)uniform readonly image2D matteImage;layout(set=0,binding=3,rgba32f)uniform writeonly image2D outputImage;layout(push_constant)uniform P{uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(id.x>=p.width||id.y>=p.height)return;float m=clamp(imageLoad(matteImage,ivec2(id)).r,0.0,1.0);imageStore(outputImage,ivec2(id),mix(imageLoad(originalImage,ivec2(id)),imageLoad(processedImage,ivec2(id)),m));})";
constexpr std::string_view kMixerGles=R"(#version 310 es
precision highp float;precision highp int;layout(local_size_x=8,local_size_y=8,local_size_z=1)in;layout(binding=0,rgba32f)uniform readonly highp image2D inputA;layout(binding=1,rgba32f)uniform readonly highp image2D inputB;layout(binding=2,rgba32f)uniform writeonly highp image2D outputImage;layout(std140,binding=3)uniform P{float weightA;float weightB;uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(id.x>=p.width||id.y>=p.height)return;float a=max(p.weightA,0.0),b=max(p.weightB,0.0),s=max(a+b,1e-20);imageStore(outputImage,ivec2(id),(imageLoad(inputA,ivec2(id))*a+imageLoad(inputB,ivec2(id))*b)/s);})";
constexpr std::string_view kMaskGles=R"(#version 310 es
precision highp float;precision highp int;layout(local_size_x=8,local_size_y=8,local_size_z=1)in;layout(binding=0,rgba32f)uniform readonly highp image2D originalImage;layout(binding=1,rgba32f)uniform readonly highp image2D processedImage;layout(binding=2,r32f)uniform readonly highp image2D matteImage;layout(binding=3,rgba32f)uniform writeonly highp image2D outputImage;layout(std140,binding=4)uniform P{uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(id.x>=p.width||id.y>=p.height)return;float m=clamp(imageLoad(matteImage,ivec2(id)).r,0.0,1.0);imageStore(outputImage,ivec2(id),mix(imageLoad(originalImage,ivec2(id)),imageLoad(processedImage,ivec2(id)),m));})";
constexpr std::string_view kMixerMsl=R"(#include <metal_stdlib>
using namespace metal;struct P{float weightA;float weightB;uint width;uint height;};kernel void node_mixer(texture2d<float,access::read>a[[texture(0)]],texture2d<float,access::read>b[[texture(1)]],texture2d<float,access::write>o[[texture(2)]],constant P&p[[buffer(0)]],uint2 id[[thread_position_in_grid]]){if(id.x>=p.width||id.y>=p.height)return;float wa=max(p.weightA,0.0f),wb=max(p.weightB,0.0f),s=max(wa+wb,1e-20f);o.write((a.read(id)*wa+b.read(id)*wb)/s,id);})";
constexpr std::string_view kMaskMsl=R"(#include <metal_stdlib>
using namespace metal;struct P{uint width;uint height;};kernel void masked_composite(texture2d<float,access::read>a[[texture(0)]],texture2d<float,access::read>b[[texture(1)]],texture2d<float,access::read>m[[texture(2)]],texture2d<float,access::write>o[[texture(3)]],constant P&p[[buffer(0)]],uint2 id[[thread_position_in_grid]]){if(id.x>=p.width||id.y>=p.height)return;float t=clamp(m.read(id).r,0.0f,1.0f);o.write(mix(a.read(id),b.read(id),t),id);})";
}
NativeNodePipelineContract native_node_pipeline_contract(DigitorRendererBackend backend,NativeNodeKernel kernel) noexcept{
 const bool mix=kernel==NativeNodeKernel::parallel_mixer; const auto* bindings=mix?kMixerBindings:kMaskBindings; const auto count=mix?4u:5u; const auto bytes=mix?16u:8u;
 switch(backend){
  case DIGITOR_RENDERER_VULKAN:return {NativeNodeShaderLanguage::spirv_glsl,"main",mix?kMixerVk:kMaskVk,bindings,count,bytes,8,8,1,true};
  case DIGITOR_RENDERER_D3D12:return {NativeNodeShaderLanguage::hlsl,"main",mix?kMixerHlsl:kMaskHlsl,bindings,count,bytes,8,8,1,false};
  case DIGITOR_RENDERER_METAL:return {NativeNodeShaderLanguage::metal,mix?"node_mixer":"masked_composite",mix?kMixerMsl:kMaskMsl,bindings,count,bytes,8,8,1,false};
  case DIGITOR_RENDERER_OPENGL_ES:return {NativeNodeShaderLanguage::gles_glsl,"main",mix?kMixerGles:kMaskGles,bindings,count,bytes,8,8,1,false};
  default:return {};
 }
}
NativeNodeShaderContract native_node_shader_contract(DigitorRendererBackend b,NativeNodeKernel k) noexcept{return native_node_pipeline_contract(b,k);}
bool validate_native_node_pipeline_contract(const NativeNodePipelineContract& c) noexcept{
 if(c.language==NativeNodeShaderLanguage::unknown||c.entry_point.empty()||c.source.empty()||!c.bindings||c.binding_count==0||c.constant_bytes==0||c.local_size_x==0||c.local_size_y==0||c.local_size_z==0)return false;
 for(std::uint32_t i=0;i<c.binding_count;++i){if(c.bindings[i].format.empty())return false;for(std::uint32_t j=i+1;j<c.binding_count;++j)if(c.bindings[i].binding==c.bindings[j].binding)return false;}
 return true;
}
}
