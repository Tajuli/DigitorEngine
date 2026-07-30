// DigitorEngine native node masked-composition compute shader.
// The same source is compiled to DXIL for D3D12 and SPIR-V for Vulkan.
Texture2D<float4> Original : register(t0);
Texture2D<float4> Processed : register(t1);
Texture2D<float> Matte : register(t2);
RWTexture2D<float4> Output : register(u0);
cbuffer Params : register(b0) {
  uint Width;
  uint Height;
};
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= Width || id.y >= Height) return;
  const float m = saturate(Matte[id.xy]);
  Output[id.xy] = lerp(Original[id.xy], Processed[id.xy], m);
}
