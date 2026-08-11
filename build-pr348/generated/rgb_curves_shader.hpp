#pragma once
inline constexpr char digitor_rgb_curves_hlsl[] = R"DIGITOR_HLSL(// Canonical RGB Curves shader ABI v1. Backend artifacts are generated from
// this source; do not duplicate the curve mathematics in backend code.
struct CurveMeta { float lo, hi, first, last; float slopeBefore, slopeAfter; uint extrapolation, enabled; };
#ifdef DIGITOR_TEXTURE_OUTPUT
#ifdef DIGITOR_VULKAN
[[vk::binding(0,0)]] Texture2D<float4> sourceTexture : register(t0);
[[vk::binding(2,0)]] StructuredBuffer<float> curveLuts : register(t1);
[[vk::binding(1,0)]] RWTexture2D<float4> destinationTexture : register(u0);
[[vk::binding(3,0)]] cbuffer Parameters : register(b0) { CurveMeta curves[4]; uint lutSize; uint pixelCount; uint imageWidth; uint imageHeight; };
#else
Texture2D<float4> sourceTexture : register(t0);
StructuredBuffer<float> curveLuts : register(t1);
RWTexture2D<float4> destinationTexture : register(u0);
cbuffer Parameters : register(b0) { CurveMeta curves[4]; uint lutSize; uint pixelCount; uint imageWidth; uint imageHeight; };
#endif
#elif defined(DIGITOR_VULKAN)
[[vk::binding(0,0)]] StructuredBuffer<float4> sourcePixels : register(t0);
[[vk::binding(2,0)]] StructuredBuffer<float> curveLuts : register(t1);
[[vk::binding(1,0)]] RWStructuredBuffer<float4> destinationPixels : register(u0);
[[vk::binding(3,0)]] cbuffer Parameters : register(b0) { CurveMeta curves[4]; uint lutSize; uint pixelCount; };
#else
StructuredBuffer<float4> sourcePixels : register(t0);
StructuredBuffer<float> curveLuts : register(t1); // master, red, green, blue
RWStructuredBuffer<float4> destinationPixels : register(u0);
cbuffer Parameters : register(b0) { CurveMeta curves[4]; uint lutSize; uint pixelCount; };
#endif

float sampleCurve(uint curve, float x) {
  CurveMeta m = curves[curve];
  if (m.enabled == 0 || !isfinite(x)) return x;
  if (x < m.lo) return m.extrapolation == 2 ? m.first + m.slopeBefore * (x-m.lo) : m.first;
  if (x > m.hi) return m.extrapolation == 2 ? m.last + m.slopeAfter * (x-m.hi) : m.last;
  float u = (x-m.lo)/(m.hi-m.lo) * float(lutSize-1);
  uint a = min((uint)u, lutSize-1), b = min(a+1, lutSize-1);
  return lerp(curveLuts[curve*lutSize+a], curveLuts[curve*lutSize+b], u-float(a));
}
[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= pixelCount) return;
#ifdef DIGITOR_TEXTURE_OUTPUT
  uint2 coordinate=uint2(id.x%imageWidth,id.x/imageWidth);
  float4 c=sourceTexture.Load(int3(coordinate,0)); float alpha=c.a;
#else
  float4 c=sourcePixels[id.x]; float alpha=c.a;
#endif
  c.r=sampleCurve(0,c.r); c.g=sampleCurve(0,c.g); c.b=sampleCurve(0,c.b);
  c.r=sampleCurve(1,c.r); c.g=sampleCurve(2,c.g); c.b=sampleCurve(3,c.b);
  c.a=alpha;
#ifdef DIGITOR_TEXTURE_OUTPUT
  destinationTexture[coordinate]=c;
#else
  destinationPixels[id.x]=c;
#endif
}
)DIGITOR_HLSL";
