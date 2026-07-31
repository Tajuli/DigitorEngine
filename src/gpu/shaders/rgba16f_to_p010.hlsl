cbuffer ConversionConstants : register(b0) {
  float4 y_row;
  float4 cb_row;
  float4 cr_row;
  float y_offset;
  float y_scale;
  float uv_offset;
  float uv_scale;
  float mastering_peak_nits;
  uint width;
  uint height;
  uint transfer;
  uint flags;
};

Texture2D<float4> source_rgba : register(t0);
RWTexture2D<uint> output_y : register(u0);      // P010 plane 0 as R16_UINT
RWTexture2D<uint2> output_uv : register(u1);   // P010 plane 1 as R16G16_UINT

float pq_oetf(float linear_nits) {
  const float m1 = 2610.0 / 16384.0;
  const float m2 = 2523.0 / 32.0;
  const float c1 = 3424.0 / 4096.0;
  const float c2 = 2413.0 / 128.0;
  const float c3 = 2392.0 / 128.0;
  float l = max(linear_nits / 10000.0, 0.0);
  float p = pow(l, m1);
  return pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}

float hlg_oetf(float linear_value) {
  const float a = 0.17883277;
  const float b = 0.28466892;
  const float c = 0.55991073;
  return linear_value <= (1.0 / 12.0)
    ? sqrt(3.0 * max(linear_value, 0.0))
    : a * log(12.0 * linear_value - b) + c;
}

float encode_transfer(float value) {
  if (transfer == 2) return pq_oetf(value * mastering_peak_nits);
  if (transfer == 3) return hlg_oetf(max(value, 0.0));
  return value <= 0.0031308
    ? 12.92 * value
    : 1.055 * pow(max(value, 0.0), 1.0 / 2.4) - 0.055;
}

float3 encoded_rgb(uint2 p) {
  float3 linear_rgb = source_rgba.Load(int3(p, 0)).rgb;
  return float3(encode_transfer(linear_rgb.r),
                encode_transfer(linear_rgb.g),
                encode_transfer(linear_rgb.b));
}

uint pack_p010(float code_value) {
  float bounded = (flags & 2u) != 0u ? code_value : clamp(code_value, 0.0, 1023.0);
  return (uint)round(clamp(bounded, 0.0, 1023.0)) << 6;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 p = id.xy;
  if (p.x >= width || p.y >= height) return;

  float3 rgb = encoded_rgb(p);
  float y = dot(y_row.xyz, rgb) + y_row.w;
  output_y[p] = pack_p010(y_offset + y * y_scale);

  if ((p.x & 1u) == 0u && (p.y & 1u) == 0u) {
    uint2 p1 = uint2(min(p.x + 1u, width - 1u), p.y);
    uint2 p2 = uint2(p.x, min(p.y + 1u, height - 1u));
    uint2 p3 = uint2(p1.x, p2.y);
    float3 average_rgb = (rgb + encoded_rgb(p1) + encoded_rgb(p2) + encoded_rgb(p3)) * 0.25;
    float cb = dot(cb_row.xyz, average_rgb) + cb_row.w;
    float cr = dot(cr_row.xyz, average_rgb) + cr_row.w;
    output_uv[p >> 1] = uint2(pack_p010(uv_offset + cb * uv_scale),
                              pack_p010(uv_offset + cr * uv_scale));
  }
}
