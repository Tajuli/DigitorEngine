#include "digitor/native_node_shader_contracts.hpp"
#include "digitor/native_node_mask_kernel_contracts.hpp"

#include <array>

namespace digitor {
namespace {

constexpr std::array<NativeNodeBinding, 4> kMixerBindings{{
    {0, NativeNodeBindingKind::sampled_or_storage_input, "rgba32f"},
    {1, NativeNodeBindingKind::sampled_or_storage_input, "rgba32f"},
    {2, NativeNodeBindingKind::storage_output, "rgba32f"},
    {3, NativeNodeBindingKind::constants, "16-bytes"},
}};
constexpr std::array<NativeNodeBinding, 5> kCompositeBindings{{
    {0, NativeNodeBindingKind::sampled_or_storage_input, "rgba32f"},
    {1, NativeNodeBindingKind::sampled_or_storage_input, "rgba32f"},
    {2, NativeNodeBindingKind::sampled_or_storage_input, "r32f"},
    {3, NativeNodeBindingKind::storage_output, "rgba32f"},
    {4, NativeNodeBindingKind::constants, "8-bytes"},
}};

constexpr std::string_view kMixerHlsl = R"(
Texture2D<float4> InputA : register(t0);
Texture2D<float4> InputB : register(t1);
RWTexture2D<float4> Output : register(u0);
cbuffer Params : register(b0) { float weightA; float weightB; uint width; uint height; };
[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  float a=max(weightA,0.0), b=max(weightB,0.0), s=max(a+b,1e-20);
  Output[id.xy]=(InputA[id.xy]*a+InputB[id.xy]*b)/s;
})";
constexpr std::string_view kCompositeHlsl = R"(
Texture2D<float4> Original : register(t0);
Texture2D<float4> Processed : register(t1);
Texture2D<float> Matte : register(t2);
RWTexture2D<float4> Output : register(u0);
cbuffer Params : register(b0) { uint width; uint height; };
[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  Output[id.xy]=lerp(Original[id.xy],Processed[id.xy],saturate(Matte[id.xy]));
})";
constexpr std::string_view kHslHlsl = R"(
struct Range{float low;float high;float softness;float padding;};
Texture2D<float4> Source:register(t0);RWTexture2D<float> Matte:register(u0);
cbuffer Params:register(b0){Range hue_range;Range saturation_range;Range luminance_range;float clean_black;float clean_white;float denoise;float blur;uint invert;uint width;uint height;uint padding;};
float lw(float v,Range r){if(v>=r.low&&v<=r.high)return 1;if(r.softness>0&&v<r.low&&v>r.low-r.softness)return(v-r.low+r.softness)/r.softness;if(r.softness>0&&v>r.high&&v<r.high+r.softness)return(r.high+r.softness-v)/r.softness;return 0;}
float hw(float h,Range r){if(r.low<=r.high)return lw(h,r);Range a=r;a.high=1;Range b=r;b.low=0;return max(lw(h,a),lw(h,b));}
float3 hsl(float3 c){float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b)),d=hi-lo,L=(hi+lo)*.5,S=d==0?0:d/max(1e-8,1-abs(2*L-1)),H=0;if(d!=0){if(hi==c.r)H=fmod((c.g-c.b)/d,6);else if(hi==c.g)H=(c.b-c.r)/d+2;else H=(c.r-c.g)/d+4;H/=6;if(H<0)H+=1;}return float3(H,S,L);}
float base_at(int2 q){q=clamp(q,int2(0,0),int2(int(width)-1,int(height)-1));float3 rgb=Source.Load(int3(q,0)).rgb;float m=0;if(all(isfinite(rgb))){float3 v=hsl(rgb);m=hw(v.x,hue_range)*lw(v.y,saturation_range)*lw(v.z,luminance_range);if(m<=clean_black)m=0;if(m>=1-clean_white)m=1;if(invert)m=1-m;}return saturate(m);}
float dn(int2 q){float v[9];int k=0;[unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++)v[k++]=base_at(q+int2(x,y));[unroll]for(int i=1;i<9;i++){float a=v[i];int j=i-1;while(j>=0&&v[j]>a){v[j+1]=v[j];j--;}v[j+1]=a;}float b=base_at(q);return b+saturate(denoise)*(v[4]-b);}
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(id.x>=width||id.y>=height)return;int2 q=int2(id.xy);int r=int(ceil(max(blur,0)));float sum=0;uint count=0;if(r==0){sum=dn(q);count=1;}else for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++){sum+=dn(q+int2(x,y));count++;}Matte[id.xy]=saturate(sum/count);}
)";
constexpr std::string_view kWindowHlsl = R"(
RWTexture2D<float> Matte : register(u0);
cbuffer Params : register(b0) {
 float center_x; float center_y; float window_width; float window_height;
 float rotation_radians; float feather; float opacity; uint shape;
 uint invert; uint width; uint height; uint padding;
};
float fw(float d,float e,float f){if(f<=0.0)return d<=e?1.0:0.0;return 1.0-smoothstep(e,e+f,d);}
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){if(id.x>=width||id.y>=height)return;float2 uv=(float2(id.xy)+.5)/float2(width,height);float2 p=uv-float2(center_x,center_y);float c=cos(-rotation_radians),s=sin(-rotation_radians);p=float2(c*p.x-s*p.y,s*p.x+c*p.y);float hw=max(window_width*.5,1e-6),hh=max(window_height*.5,1e-6),m=0.0;if(shape==0){float2 q=abs(p)-float2(hw,hh);m=fw(length(max(q,0.0))+min(max(q.x,q.y),0.0),0.0,feather);}else if(shape==1){m=fw(length(float2(p.x/hw,p.y/hh)),1.0,feather/max(min(hw,hh),1e-6));}else{m=1.0-smoothstep(-feather,feather,p.x/hw);}m=saturate(m)*saturate(opacity);if(invert!=0)m=1.0-m;Matte[id.xy]=saturate(m);}
)";
constexpr std::string_view kMultiplyHlsl = R"(
Texture2D<float> A : register(t0); Texture2D<float> B : register(t1);
RWTexture2D<float> Output : register(u0);
cbuffer Params : register(b0) { uint width; uint height; };
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){if(id.x>=width||id.y>=height)return;Output[id.xy]=saturate(A[id.xy])*saturate(B[id.xy]);}
)";

constexpr std::string_view kMixerVk = R"(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1)in;layout(set=0,binding=0,rgba32f)uniform readonly image2D a;layout(set=0,binding=1,rgba32f)uniform readonly image2D b;layout(set=0,binding=2,rgba32f)uniform writeonly image2D o;layout(push_constant)uniform P{float wa;float wb;uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;float x=max(p.wa,0.0),y=max(p.wb,0.0),s=max(x+y,1e-20);imageStore(o,ivec2(id),(imageLoad(a,ivec2(id))*x+imageLoad(b,ivec2(id))*y)/s);})";
constexpr std::string_view kCompositeVk = R"(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1)in;layout(set=0,binding=0,rgba32f)uniform readonly image2D a;layout(set=0,binding=1,rgba32f)uniform readonly image2D b;layout(set=0,binding=2,r32f)uniform readonly image2D m;layout(set=0,binding=3,rgba32f)uniform writeonly image2D o;layout(push_constant)uniform P{uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;float t=clamp(imageLoad(m,ivec2(id)).r,0.0,1.0);imageStore(o,ivec2(id),mix(imageLoad(a,ivec2(id)),imageLoad(b,ivec2(id)),t));})";
constexpr std::string_view kHslVk = R"(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1)in;layout(set=0,binding=0,rgba32f)uniform readonly image2D src;layout(set=0,binding=1,r32f)uniform writeonly image2D outm;struct R{float low;float high;float softness;float pad;};layout(push_constant)uniform P{R hr;R sr;R lr;float cb;float cw;uint inv;uint width;uint height;}p;float lw(float v,R r){if(v>=r.low&&v<=r.high)return 1.0;if(r.softness>0.0&&v<r.low&&v>r.low-r.softness)return(v-r.low+r.softness)/r.softness;if(r.softness>0.0&&v>r.high&&v<r.high+r.softness)return(r.high+r.softness-v)/r.softness;return 0.0;}float hw(float h,R r){if(r.low<=r.high)return lw(h,r);R a=r;a.high=1.0;R b=r;b.low=0.0;return max(lw(h,a),lw(h,b));}vec3 hsl(vec3 c){float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b)),d=hi-lo,L=(hi+lo)*.5,S=d==0.0?0.0:d/max(1e-8,1.0-abs(2.0*L-1.0)),H=0.0;if(d!=0.0){if(hi==c.r)H=mod((c.g-c.b)/d,6.0);else if(hi==c.g)H=(c.b-c.r)/d+2.0;else H=(c.r-c.g)/d+4.0;H/=6.0;if(H<0.0)H+=1.0;}return vec3(H,S,L);}void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;vec3 rgb=imageLoad(src,ivec2(id)).rgb;float m=0.0;if(!any(isnan(rgb))&&!any(isinf(rgb))){vec3 v=hsl(rgb);m=hw(v.x,p.hr)*lw(v.y,p.sr)*lw(v.z,p.lr);if(m<=p.cb)m=0.0;if(m>=1.0-p.cw)m=1.0;if(p.inv!=0)m=1.0-m;}imageStore(outm,ivec2(id),vec4(clamp(m,0.0,1.0)));})";
constexpr std::string_view kWindowVk = R"(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1)in;layout(set=0,binding=0,r32f)uniform writeonly image2D outm;layout(push_constant)uniform P{float cx;float cy;float ww;float wh;float rot;float feather;float opacity;uint shape;uint inv;uint width;uint height;uint pad;}p;float fw(float d,float e,float f){if(f<=0.0)return d<=e?1.0:0.0;return 1.0-smoothstep(e,e+f,d);}void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;vec2 uv=(vec2(id)+.5)/vec2(p.width,p.height),q=uv-vec2(p.cx,p.cy);float c=cos(-p.rot),s=sin(-p.rot);q=vec2(c*q.x-s*q.y,s*q.x+c*q.y);float hw=max(p.ww*.5,1e-6),hh=max(p.wh*.5,1e-6),m=0.0;if(p.shape==0){vec2 z=abs(q)-vec2(hw,hh);m=fw(length(max(z,vec2(0)))+min(max(z.x,z.y),0.0),0.0,p.feather);}else if(p.shape==1)m=fw(length(vec2(q.x/hw,q.y/hh)),1.0,p.feather/max(min(hw,hh),1e-6));else m=1.0-smoothstep(-p.feather,p.feather,q.x/hw);m=clamp(m,0.0,1.0)*clamp(p.opacity,0.0,1.0);if(p.inv!=0)m=1.0-m;imageStore(outm,ivec2(id),vec4(clamp(m,0.0,1.0)));})";
constexpr std::string_view kMultiplyVk = R"(#version 450
layout(local_size_x=8,local_size_y=8,local_size_z=1)in;layout(set=0,binding=0,r32f)uniform readonly image2D a;layout(set=0,binding=1,r32f)uniform readonly image2D b;layout(set=0,binding=2,r32f)uniform writeonly image2D o;layout(push_constant)uniform P{uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;float m=clamp(imageLoad(a,ivec2(id)).r,0.0,1.0)*clamp(imageLoad(b,ivec2(id)).r,0.0,1.0);imageStore(o,ivec2(id),vec4(m));})";

constexpr std::string_view kMixerGles = R"(#version 310 es
precision highp float;precision highp int;layout(local_size_x=8,local_size_y=8)in;layout(binding=0)uniform highp sampler2D a;layout(binding=1)uniform highp sampler2D b;layout(binding=2,rgba32f)uniform writeonly highp image2D o;layout(std140,binding=3)uniform P{float wa;float wb;uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;float x=max(p.wa,0.0),y=max(p.wb,0.0),s=max(x+y,1e-20);ivec2 q=ivec2(id);imageStore(o,q,(texelFetch(a,q,0)*x+texelFetch(b,q,0)*y)/s);})";
constexpr std::string_view kCompositeGles = R"(#version 310 es
precision highp float;precision highp int;layout(local_size_x=8,local_size_y=8)in;layout(binding=0)uniform highp sampler2D a;layout(binding=1)uniform highp sampler2D b;layout(binding=2)uniform highp sampler2D m;layout(binding=3,rgba32f)uniform writeonly highp image2D o;layout(std140,binding=4)uniform P{uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;ivec2 q=ivec2(id);float t=clamp(texelFetch(m,q,0).r,0.0,1.0);imageStore(o,q,mix(texelFetch(a,q,0),texelFetch(b,q,0),t));})";
constexpr std::string_view kHslGles = R"(#version 310 es
precision highp float;precision highp int;layout(local_size_x=8,local_size_y=8)in;layout(binding=0)uniform highp sampler2D src;layout(binding=1,r32f)uniform writeonly highp image2D outm;layout(std140,binding=2)uniform P{vec4 hr;vec4 sr;vec4 lr;float cb;float cw;float denoise;float blur;uint inv;uint width;uint height;uint pad;}p;
float lw(float v,vec4 r){if(v>=r.x&&v<=r.y)return 1.0;if(r.z>0.0&&v<r.x&&v>r.x-r.z)return(v-r.x+r.z)/r.z;if(r.z>0.0&&v>r.y&&v<r.y+r.z)return(r.y+r.z-v)/r.z;return 0.0;}float hw(float h,vec4 r){if(r.x<=r.y)return lw(h,r);vec4 a=r;a.y=1.0;vec4 b=r;b.x=0.0;return max(lw(h,a),lw(h,b));}
vec3 hsl(vec3 c){float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b)),d=hi-lo,L=(hi+lo)*.5,S=d==0.0?0.0:d/max(1e-8,1.0-abs(2.0*L-1.0)),H=0.0;if(d!=0.0){if(hi==c.r)H=mod((c.g-c.b)/d,6.0);else if(hi==c.g)H=(c.b-c.r)/d+2.0;else H=(c.r-c.g)/d+4.0;H/=6.0;if(H<0.0)H+=1.0;}return vec3(H,S,L);}float base_at(ivec2 q){q=clamp(q,ivec2(0),ivec2(int(p.width)-1,int(p.height)-1));vec3 v=hsl(texelFetch(src,q,0).rgb);float m=hw(v.x,p.hr)*lw(v.y,p.sr)*lw(v.z,p.lr);if(m<=p.cb)m=0.0;if(m>=1.0-p.cw)m=1.0;if(p.inv!=0u)m=1.0-m;return clamp(m,0.0,1.0);}float dn(ivec2 q){float v[9];int k=0;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)v[k++]=base_at(q+ivec2(x,y));for(int i=1;i<9;i++){float a=v[i];int j=i-1;while(j>=0&&v[j]>a){v[j+1]=v[j];j--; }v[j+1]=a;}float b=base_at(q);return b+clamp(p.denoise,0.0,1.0)*(v[4]-b);}void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;ivec2 q=ivec2(id);int r=int(ceil(max(p.blur,0.0)));float sum=0.0;int count=0;if(r==0){sum=dn(q);count=1;}else for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++){sum+=dn(q+ivec2(x,y));count++;}imageStore(outm,q,vec4(clamp(sum/float(count),0.0,1.0)));})";
constexpr std::string_view kWindowGles = R"(#version 310 es
precision highp float;precision highp int;layout(local_size_x=8,local_size_y=8)in;layout(binding=0,r32f)uniform writeonly highp image2D outm;layout(std140,binding=1)uniform P{float cx;float cy;float ww;float wh;float rot;float feather;float opacity;uint shape;uint inv;uint width;uint height;uint pad;}p;float fw(float d,float e,float f){if(f<=0.0)return d<=e?1.0:0.0;return 1.0-smoothstep(e,e+f,d);}void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;vec2 uv=(vec2(id)+.5)/vec2(p.width,p.height),q=uv-vec2(p.cx,p.cy);float c=cos(-p.rot),s=sin(-p.rot);q=vec2(c*q.x-s*q.y,s*q.x+c*q.y);float hw=max(p.ww*.5,1e-6),hh=max(p.wh*.5,1e-6),m=0.0;if(p.shape==0u){vec2 z=abs(q)-vec2(hw,hh);m=fw(length(max(z,vec2(0)))+min(max(z.x,z.y),0.0),0.0,p.feather);}else if(p.shape==1u)m=fw(length(vec2(q.x/hw,q.y/hh)),1.0,p.feather/max(min(hw,hh),1e-6));else m=1.0-smoothstep(-p.feather,p.feather,q.x/hw);m=clamp(m,0.0,1.0)*clamp(p.opacity,0.0,1.0);if(p.inv!=0u)m=1.0-m;imageStore(outm,ivec2(id),vec4(clamp(m,0.0,1.0)));})";
constexpr std::string_view kMultiplyGles = R"(#version 310 es
precision highp float;precision highp int;layout(local_size_x=8,local_size_y=8)in;layout(binding=0)uniform highp sampler2D a;layout(binding=1)uniform highp sampler2D b;layout(binding=2,r32f)uniform writeonly highp image2D o;layout(std140,binding=3)uniform P{uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;ivec2 q=ivec2(id);imageStore(o,q,vec4(clamp(texelFetch(a,q,0).r,0.0,1.0)*clamp(texelFetch(b,q,0).r,0.0,1.0)));})";
constexpr std::string_view kMixerMsl = R"(#include <metal_stdlib>
using namespace metal;struct P{float wa;float wb;uint width;uint height;};kernel void node_mixer(texture2d<float,access::read>a[[texture(0)]],texture2d<float,access::read>b[[texture(1)]],texture2d<float,access::write>o[[texture(2)]],constant P&p[[buffer(0)]],uint2 id[[thread_position_in_grid]]){if(id.x>=p.width||id.y>=p.height)return;float x=max(p.wa,0.0f),y=max(p.wb,0.0f),s=max(x+y,1e-20f);o.write((a.read(id)*x+b.read(id)*y)/s,id);})";
constexpr std::string_view kCompositeMsl = R"(#include <metal_stdlib>
using namespace metal;struct P{uint width;uint height;};kernel void masked_composite(texture2d<float,access::read>a[[texture(0)]],texture2d<float,access::read>b[[texture(1)]],texture2d<float,access::read>m[[texture(2)]],texture2d<float,access::write>o[[texture(3)]],constant P&p[[buffer(0)]],uint2 id[[thread_position_in_grid]]){if(id.x>=p.width||id.y>=p.height)return;o.write(mix(a.read(id),b.read(id),clamp(m.read(id).r,0.0f,1.0f)),id);})";
constexpr std::string_view kHslMsl = R"(#include <metal_stdlib>
using namespace metal;struct R{float low,high,softness,pad;};struct P{R hr,sr,lr;float cb,cw,denoise,blur;uint inv,width,height,pad;};
float lw(float v,R r){if(v>=r.low&&v<=r.high)return 1.0;if(r.softness>0&&v<r.low&&v>r.low-r.softness)return(v-r.low+r.softness)/r.softness;if(r.softness>0&&v>r.high&&v<r.high+r.softness)return(r.high+r.softness-v)/r.softness;return 0.0;}float hw(float h,R r){if(r.low<=r.high)return lw(h,r);R a=r;a.high=1;R b=r;b.low=0;return max(lw(h,a),lw(h,b));}
float3 hsl(float3 c){float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b)),d=hi-lo,L=(hi+lo)*.5,S=d==0?0:d/max(1e-8f,1-abs(2*L-1)),H=0;if(d!=0){if(hi==c.r)H=fmod((c.g-c.b)/d,6.0f);else if(hi==c.g)H=(c.b-c.r)/d+2;else H=(c.r-c.g)/d+4;H/=6;if(H<0)H+=1;}return float3(H,S,L);}float base_at(texture2d<float,access::read>s,int2 q,constant P&p){q=clamp(q,int2(0),int2(int(p.width)-1,int(p.height)-1));float3 v=hsl(s.read(uint2(q)).rgb);float m=hw(v.x,p.hr)*lw(v.y,p.sr)*lw(v.z,p.lr);if(m<=p.cb)m=0;if(m>=1-p.cw)m=1;if(p.inv)m=1-m;return clamp(m,0.0f,1.0f);}float dn(texture2d<float,access::read>s,int2 q,constant P&p){float v[9];int k=0;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)v[k++]=base_at(s,q+int2(x,y),p);for(int i=1;i<9;i++){float a=v[i];int j=i-1;while(j>=0&&v[j]>a){v[j+1]=v[j];j--;}v[j+1]=a;}float b=base_at(s,q,p);return b+clamp(p.denoise,0.0f,1.0f)*(v[4]-b);}kernel void hsl_matte(texture2d<float,access::read>s[[texture(0)]],texture2d<float,access::write>o[[texture(1)]],constant P&p[[buffer(0)]],uint2 id[[thread_position_in_grid]]){if(id.x>=p.width||id.y>=p.height)return;int2 q=int2(id);int r=int(ceil(max(p.blur,0.0f)));float sum=0;uint count=0;if(r==0){sum=dn(s,q,p);count=1;}else for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++){sum+=dn(s,q+int2(x,y),p);count++;}o.write(float4(clamp(sum/float(count),0.0f,1.0f)),id);})";
constexpr std::string_view kWindowMsl = R"(#include <metal_stdlib>
using namespace metal;struct P{float cx,cy,ww,wh,rot,feather,opacity;uint shape,inv,width,height,pad;};float fw(float d,float e,float f){if(f<=0)return d<=e?1:0;return 1-smoothstep(e,e+f,d);}kernel void power_window_matte(texture2d<float,access::write>o[[texture(0)]],constant P&p[[buffer(0)]],uint2 id[[thread_position_in_grid]]){if(id.x>=p.width||id.y>=p.height)return;float2 uv=(float2(id)+.5)/float2(p.width,p.height),q=uv-float2(p.cx,p.cy);float c=cos(-p.rot),s=sin(-p.rot);q=float2(c*q.x-s*q.y,s*q.x+c*q.y);float hw=max(p.ww*.5f,1e-6f),hh=max(p.wh*.5f,1e-6f),m=0;if(p.shape==0){float2 z=abs(q)-float2(hw,hh);m=fw(length(max(z,float2(0)))+min(max(z.x,z.y),0.0f),0,p.feather);}else if(p.shape==1)m=fw(length(float2(q.x/hw,q.y/hh)),1,p.feather/max(min(hw,hh),1e-6f));else m=1-smoothstep(-p.feather,p.feather,q.x/hw);m=clamp(m,0.0f,1.0f)*clamp(p.opacity,0.0f,1.0f);if(p.inv)m=1-m;o.write(float4(clamp(m,0.0f,1.0f)),id);})";
constexpr std::string_view kMultiplyMsl = R"(#include <metal_stdlib>
using namespace metal;struct P{uint width;uint height;};kernel void matte_multiply(texture2d<float,access::read>a[[texture(0)]],texture2d<float,access::read>b[[texture(1)]],texture2d<float,access::write>o[[texture(2)]],constant P&p[[buffer(0)]],uint2 id[[thread_position_in_grid]]){if(id.x>=p.width||id.y>=p.height)return;o.write(float4(clamp(a.read(id).r,0.0f,1.0f)*clamp(b.read(id).r,0.0f,1.0f)),id);})";

struct KernelData {
  const NativeNodeBinding* bindings{};
  std::uint32_t binding_count{};
  std::uint32_t constant_bytes{};
  std::string_view hlsl, vk, gles, msl, metal_entry;
};

KernelData data_for(NativeNodeKernel kernel) noexcept {
  switch (kernel) {
    case NativeNodeKernel::parallel_mixer:
      return {kMixerBindings.data(), static_cast<std::uint32_t>(kMixerBindings.size()), 16, kMixerHlsl, kMixerVk, kMixerGles, kMixerMsl, "node_mixer"};
    case NativeNodeKernel::masked_composite:
      return {kCompositeBindings.data(), static_cast<std::uint32_t>(kCompositeBindings.size()), 8, kCompositeHlsl, kCompositeVk, kCompositeGles, kCompositeMsl, "masked_composite"};
    case NativeNodeKernel::hsl_matte:
      return {kNativeHslMatteBindings.data(), static_cast<std::uint32_t>(kNativeHslMatteBindings.size()), kNativeHslMatteConstantBytes, kHslHlsl, kHslVk, kHslGles, kHslMsl, "hsl_matte"};
    case NativeNodeKernel::power_window_matte:
      return {kNativePowerWindowMatteBindings.data(), static_cast<std::uint32_t>(kNativePowerWindowMatteBindings.size()), kNativePowerWindowMatteConstantBytes, kWindowHlsl, kWindowVk, kWindowGles, kWindowMsl, "power_window_matte"};
    case NativeNodeKernel::matte_multiply:
      return {kNativeMatteMultiplyBindings.data(), static_cast<std::uint32_t>(kNativeMatteMultiplyBindings.size()), kNativeMatteMultiplyConstantBytes, kMultiplyHlsl, kMultiplyVk, kMultiplyGles, kMultiplyMsl, "matte_multiply"};
  }
  return {};
}

} // namespace

NativeNodePipelineContract native_node_pipeline_contract(
    DigitorRendererBackend backend, NativeNodeKernel kernel) noexcept {
  const auto d = data_for(kernel);
  if (!d.bindings || d.binding_count == 0 || d.constant_bytes == 0) return {};
  switch (backend) {
    case DIGITOR_RENDERER_VULKAN:
      return {NativeNodeShaderLanguage::spirv_glsl, "main", d.vk, d.bindings,
              d.binding_count, d.constant_bytes, 8, 8, 1, true};
    case DIGITOR_RENDERER_D3D12:
      return {NativeNodeShaderLanguage::hlsl, "main", d.hlsl, d.bindings,
              d.binding_count, d.constant_bytes, 8, 8, 1, false};
    case DIGITOR_RENDERER_METAL:
      return {NativeNodeShaderLanguage::metal, d.metal_entry, d.msl, d.bindings,
              d.binding_count, d.constant_bytes, 8, 8, 1, false};
    case DIGITOR_RENDERER_OPENGL_ES:
      return {NativeNodeShaderLanguage::gles_glsl, "main", d.gles, d.bindings,
              d.binding_count, d.constant_bytes, 8, 8, 1, false};
    default:
      return {};
  }
}

NativeNodeShaderContract native_node_shader_contract(
    DigitorRendererBackend backend, NativeNodeKernel kernel) noexcept {
  return native_node_pipeline_contract(backend, kernel);
}

bool validate_native_node_pipeline_contract(
    const NativeNodePipelineContract& contract) noexcept {
  if (contract.language == NativeNodeShaderLanguage::unknown ||
      contract.entry_point.empty() || contract.source.empty() ||
      !contract.bindings || contract.binding_count == 0 ||
      contract.constant_bytes == 0 || contract.local_size_x == 0 ||
      contract.local_size_y == 0 || contract.local_size_z == 0)
    return false;
  for (std::uint32_t i = 0; i < contract.binding_count; ++i) {
    if (contract.bindings[i].format.empty()) return false;
    for (std::uint32_t j = i + 1; j < contract.binding_count; ++j)
      if (contract.bindings[i].binding == contract.bindings[j].binding)
        return false;
  }
  return true;
}

} // namespace digitor
