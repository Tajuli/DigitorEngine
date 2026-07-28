// Canonical Primary Wheels FP32 shader ABI v1. Backends compile this source.
struct Wheel { float3 rgb; float master; uint enabled; float3 padding; };
#ifdef DIGITOR_TEXTURE_OUTPUT
#ifdef DIGITOR_VULKAN
[[vk::binding(0,0)]] Texture2D<float4> sourceTexture : register(t0);
[[vk::binding(1,0)]] RWTexture2D<float4> destinationTexture : register(u0);
[[vk::binding(2,0)]] cbuffer Parameters : register(b0) { Wheel lift; Wheel gamma; Wheel gain; Wheel offset; uint pixelCount; uint imageWidth; uint imageHeight; uint padding0; };
#else
Texture2D<float4> sourceTexture : register(t0); RWTexture2D<float4> destinationTexture : register(u0);
cbuffer Parameters : register(b0) { Wheel lift; Wheel gamma; Wheel gain; Wheel offset; uint pixelCount; uint imageWidth; uint imageHeight; uint padding0; };
#endif
#else
StructuredBuffer<float4> sourcePixels : register(t0); RWStructuredBuffer<float4> destinationPixels : register(u0);
cbuffer Parameters : register(b0) { Wheel lift; Wheel gamma; Wheel gain; Wheel offset; uint pixelCount; uint imageWidth; uint imageHeight; uint padding0; };
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
  if(lift.enabled)c.rgb+=lift.rgb+lift.master;
  if(gamma.enabled)c.rgb=float3(signedPow(c.r,1.0/(gamma.r*gamma.master)),signedPow(c.g,1.0/(gamma.g*gamma.master)),signedPow(c.b,1.0/(gamma.b*gamma.master)));
  if(gain.enabled)c.rgb*=gain.rgb*gain.master;
  if(offset.enabled)c.rgb+=offset.rgb+offset.master;
  c.a=alpha;
#ifdef DIGITOR_TEXTURE_OUTPUT
  destinationTexture[coordinate]=c;
#else
  destinationPixels[id.x]=c;
#endif
}
