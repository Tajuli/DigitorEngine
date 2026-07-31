#include <metal_stdlib>
using namespace metal;

struct ConvertParams {
  float3x3 rgb_to_yuv;
  float3 offset;
  float3 scale;
  uint width;
  uint height;
  uint transfer;
  float mastering_peak_nits;
};

static float encode_transfer(float v, uint transfer, float peak) {
  v = max(v, 0.0f);
  if (transfer == 2u) {
    const float m1 = 2610.0f / 16384.0f;
    const float m2 = 2523.0f / 32.0f;
    const float c1 = 3424.0f / 4096.0f;
    const float c2 = 2413.0f / 128.0f;
    const float c3 = 2392.0f / 128.0f;
    float x = powr(clamp(v * peak / 10000.0f, 0.0f, 1.0f), m1);
    return powr((c1 + c2 * x) / (1.0f + c3 * x), m2);
  }
  if (transfer == 3u)
    return v <= (1.0f / 12.0f) ? sqrt(3.0f * v) : 0.17883277f * log(12.0f * v - 0.28466892f) + 0.55991073f;
  return powr(clamp(v, 0.0f, 1.0f), 1.0f / 2.4f);
}

kernel void rgba16f_to_p010(texture2d<half, access::read> source [[texture(0)]],
                            texture2d<half, access::write> yPlane [[texture(1)]],
                            texture2d<half2, access::write> uvPlane [[texture(2)]],
                            constant ConvertParams& p [[buffer(0)]],
                            uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= p.width || gid.y >= p.height) return;
  float3 rgb = float3(source.read(gid).rgb);
  rgb = float3(encode_transfer(rgb.r, p.transfer, p.mastering_peak_nits),
               encode_transfer(rgb.g, p.transfer, p.mastering_peak_nits),
               encode_transfer(rgb.b, p.transfer, p.mastering_peak_nits));
  float3 yuv = p.rgb_to_yuv * rgb + p.offset;
  yPlane.write(half(clamp(yuv.x * p.scale.x, 0.0f, 1.0f)), gid);
  if (((gid.x | gid.y) & 1u) == 0u) {
    float2 uv = float2(0.0f);
    for (uint oy = 0; oy < 2; ++oy)
      for (uint ox = 0; ox < 2; ++ox) {
        uint2 q = min(gid + uint2(ox, oy), uint2(p.width - 1, p.height - 1));
        float3 c = float3(source.read(q).rgb);
        c = float3(encode_transfer(c.r, p.transfer, p.mastering_peak_nits),
                   encode_transfer(c.g, p.transfer, p.mastering_peak_nits),
                   encode_transfer(c.b, p.transfer, p.mastering_peak_nits));
        uv += (p.rgb_to_yuv * c + p.offset).yz;
      }
    uv *= 0.25f;
    uvPlane.write(half2(clamp(uv * p.scale.yz, 0.0f, 1.0f)), gid / 2u);
  }
}
