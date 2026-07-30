// Multiplies two node-local R32F mattes. Additional mattes are folded by repeated dispatch.
Texture2D<float> MatteA : register(t0);
Texture2D<float> MatteB : register(t1);
RWTexture2D<float> Output : register(u0);
cbuffer Params : register(b0) {
  uint width;
  uint height;
};
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const float a = saturate(MatteA.Load(int3(id.xy, 0)));
  const float b = saturate(MatteB.Load(int3(id.xy, 0)));
  Output[id.xy] = a * b;
}
