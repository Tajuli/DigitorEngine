// Canonical RGB Curves shader ABI v1. Backend artifacts are generated from
// this source; do not duplicate the curve mathematics in backend code.
struct CurveMeta { float lo, hi, first, last; float slopeBefore, slopeAfter; uint extrapolation, enabled; };
StructuredBuffer<float4> sourcePixels : register(t0);
StructuredBuffer<float> curveLuts : register(t1); // master, red, green, blue
RWStructuredBuffer<float4> destinationPixels : register(u0);
cbuffer Parameters : register(b0) { CurveMeta curves[4]; uint lutSize; uint pixelCount; };

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
  float4 c=sourcePixels[id.x]; float alpha=c.a;
  c.r=sampleCurve(0,c.r); c.g=sampleCurve(0,c.g); c.b=sampleCurve(0,c.b);
  c.r=sampleCurve(1,c.r); c.g=sampleCurve(2,c.g); c.b=sampleCurve(3,c.b);
  c.a=alpha; destinationPixels[id.x]=c;
}
