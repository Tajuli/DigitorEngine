// DigitorEngine native two-input weighted parallel-node mixer.
Texture2D<float4> InputA : register(t0);
Texture2D<float4> InputB : register(t1);
RWTexture2D<float4> Output : register(u0);
cbuffer Params : register(b0) {
  float WeightA;
  float WeightB;
  uint Width;
  uint Height;
};
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= Width || id.y >= Height) return;
  const float a = max(WeightA, 0.0);
  const float b = max(WeightB, 0.0);
  const float sum = max(a + b, 1.0e-20);
  Output[id.xy] = (InputA[id.xy] * a + InputB[id.xy] * b) / sum;
}
