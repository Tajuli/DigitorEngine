#pragma once
namespace digitor { inline constexpr char digitor_primary_wheels_hlsl[] = R"hlsl(// Canonical Primary Wheels FP32 shader ABI v1. Backends compile this source.
#ifdef DIGITOR_TEXTURE_OUTPUT
#ifdef DIGITOR_VULKAN
[[vk::binding(0,0)]] Texture2D<float4> sourceTexture : register(t0);
[[vk::binding(1,0)]] RWTexture2D<float4> destinationTexture : register(u0);
[[vk::binding(2,0)]] cbuffer Parameters : register(b0) { float4 lift; float4 gamma; float4 gain; float4 offset; uint4 enabled; uint pixelCount; uint imageWidth; uint imageHeight; uint padding0; };
#else
Texture2D<float4> sourceTexture : register(t0); RWTexture2D<float4> destinationTexture : register(u0);
cbuffer Parameters : register(b0) { float4 lift; float4 gamma; float4 gain; float4 offset; uint4 enabled; uint pixelCount; uint imageWidth; uint imageHeight; uint padding0; };
#endif
#else
StructuredBuffer<float4> sourcePixels : register(t0); RWStructuredBuffer<float4> destinationPixels : register(u0);
cbuffer Parameters : register(b0) { float4 lift; float4 gamma; float4 gain; float4 offset; uint4 enabled; uint pixelCount; uint imageWidth; uint imageHeight; uint padding0; };
#endif
float signedPow(float x,float e) { return !isfinite(x) ? x : (x < 0 ? -pow(-x,e) : pow(x,e)); }
[numthreads(64,1,1)] void main(uint3 id:SV_DispatchThreadID) {
  if(id.x>=pixelCount)return;
#ifdef DIGITOR_TEXTURE_OUTPUT
  uint2 coordinate=uint2(id.x%imageWidth,id.x/imageWidth);float4 c=sourceTexture.Load(int3(coordinate,0));
#else
  float4 c=sourcePixels[id.x];
#endif
  float alpha=c.a;
  if(enabled.x)c.rgb+=lift.rgb+lift.a;
  if(enabled.y)c.rgb=float3(signedPow(c.r,1.0/(gamma.r*gamma.a)),signedPow(c.g,1.0/(gamma.g*gamma.a)),signedPow(c.b,1.0/(gamma.b*gamma.a)));
  if(enabled.z)c.rgb*=gain.rgb*gain.a;
  if(enabled.w)c.rgb+=offset.rgb+offset.a;
  c.a=alpha;
#ifdef DIGITOR_TEXTURE_OUTPUT
  destinationTexture[coordinate]=c;
#else
  destinationPixels[id.x]=c;
#endif
}
)hlsl"; }
