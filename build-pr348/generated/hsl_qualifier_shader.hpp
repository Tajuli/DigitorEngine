#pragma once
namespace digitor { inline constexpr char digitor_hsl_qualifier_hlsl[] = R"hlsl(// DigitorEngine v5.0.0 GPU-first HSL qualifier native compute contract.
struct QualifierRange { float low; float high; float softness; float padding; };
#ifdef DIGITOR_TEXTURE_OUTPUT
#ifdef DIGITOR_VULKAN
[[vk::binding(0,0)]] Texture2D<float4> sourceTexture : register(t0);
[[vk::binding(1,0)]] RWTexture2D<float4> destinationTexture : register(u0);
[[vk::binding(2,0)]] cbuffer HslQualifierParameters : register(b0) {
#else
Texture2D<float4> sourceTexture : register(t0);
RWTexture2D<float4> destinationTexture : register(u0);
cbuffer HslQualifierParameters : register(b0) {
#endif
#else
StructuredBuffer<float4> sourcePixels : register(t0);
RWStructuredBuffer<float4> destinationPixels : register(u0);
cbuffer HslQualifierParameters : register(b0) {
#endif
  QualifierRange hue_range; QualifierRange saturation_range; QualifierRange luminance_range;
  float4 cleanup; uint width; uint height; uint pixel_count; uint flags;
};
float linear_weight(float v,QualifierRange r){if(v>=r.low&&v<=r.high)return 1.0;if(r.softness>0.0&&v<r.low&&v>r.low-r.softness)return(v-r.low+r.softness)/r.softness;if(r.softness>0.0&&v>r.high&&v<r.high+r.softness)return(r.high+r.softness-v)/r.softness;return 0.0;}
float hue_weight(float h,QualifierRange r){if(r.low<=r.high)return linear_weight(h,r);QualifierRange u=r;u.high=1.0;QualifierRange l=r;l.low=0.0;return max(linear_weight(h,u),linear_weight(h,l));}
float3 rgb_to_hsl(float3 c){float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b)),d=hi-lo;float lum=(hi+lo)*0.5;float sat=d==0.0?0.0:d/max(1e-8,1.0-abs(2.0*lum-1.0));float h=0.0;if(d!=0.0){if(hi==c.r)h=fmod((c.g-c.b)/d,6.0);else if(hi==c.g)h=(c.b-c.r)/d+2.0;else h=(c.r-c.g)/d+4.0;h/=6.0;if(h<0.0)h+=1.0;}return float3(h,sat,lum);}
[numthreads(64,1,1)] void main(uint3 id:SV_DispatchThreadID){uint i=id.x;if(i>=pixel_count)return;
#ifdef DIGITOR_TEXTURE_OUTPUT
uint2 coordinate=uint2(i%width,i/width);float3 rgb=sourceTexture.Load(int3(coordinate,0)).rgb;
#else
float3 rgb=sourcePixels[i].rgb;
#endif
float m=0.0;if(all(isfinite(rgb))){float3 hsl=rgb_to_hsl(rgb);m=hue_weight(hsl.x,hue_range)*linear_weight(hsl.y,saturation_range)*linear_weight(hsl.z,luminance_range);if(m<=cleanup.x)m=0.0;if(m>=1.0-cleanup.y)m=1.0;if((flags&1u)!=0)m=1.0-m;}float4 outv=float4(m,m,m,1.0);
#ifdef DIGITOR_TEXTURE_OUTPUT
destinationTexture[coordinate]=outv;
#else
destinationPixels[i]=outv;
#endif
}
)hlsl"; }
