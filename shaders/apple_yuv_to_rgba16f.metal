#include <metal_stdlib>
using namespace metal;

struct Params {
  float3x3 yuv_to_rgb;
  float3 offset;
  uint width;
  uint height;
  uint bit_depth;
};

kernel void apple_yuv_to_rgba16f(texture2d<float, access::sample> y_tex [[texture(0)]],
                                  texture2d<float, access::sample> uv_tex [[texture(1)]],
                                  texture2d<half, access::write> out_tex [[texture(2)]],
                                  constant Params& p [[buffer(0)]],
                                  uint2 gid [[thread_position_in_grid]]) {
  if (gid.x >= p.width || gid.y >= p.height) return;
  constexpr sampler s(coord::pixel, address::clamp_to_edge, filter::nearest);
  float y = y_tex.sample(s, float2(gid)).r;
  float2 uv = uv_tex.sample(s, float2(gid) * 0.5f).rg;
  float3 rgb = p.yuv_to_rgb * (float3(y, uv) + p.offset);
  out_tex.write(half4(half3(rgb), half(1.0)), gid);
}
