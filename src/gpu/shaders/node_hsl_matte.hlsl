// Backend-native node-local HSL matte. Output is single-channel R32F.
struct QualifierRange { float low; float high; float softness; float padding; };
Texture2D<float4> Source : register(t0);
RWTexture2D<float> Matte : register(u0);
cbuffer Params : register(b0) {
  QualifierRange hue_range;
  QualifierRange saturation_range;
  QualifierRange luminance_range;
  float clean_black;
  float clean_white;
  uint invert;
  uint width;
  uint height;
};
float linear_weight(float value, QualifierRange range) {
  if (value >= range.low && value <= range.high) return 1.0;
  if (range.softness > 0.0 && value < range.low && value > range.low - range.softness)
    return (value - range.low + range.softness) / range.softness;
  if (range.softness > 0.0 && value > range.high && value < range.high + range.softness)
    return (range.high + range.softness - value) / range.softness;
  return 0.0;
}
float hue_weight(float hue, QualifierRange range) {
  if (range.low <= range.high) return linear_weight(hue, range);
  QualifierRange upper = range; upper.high = 1.0;
  QualifierRange lower = range; lower.low = 0.0;
  return max(linear_weight(hue, upper), linear_weight(hue, lower));
}
float3 rgb_to_hsl(float3 color) {
  float high = max(color.r, max(color.g, color.b));
  float low = min(color.r, min(color.g, color.b));
  float delta = high - low;
  float luminance = (high + low) * 0.5;
  float saturation = delta == 0.0 ? 0.0 : delta / max(1e-8, 1.0 - abs(2.0 * luminance - 1.0));
  float hue = 0.0;
  if (delta != 0.0) {
    if (high == color.r) hue = fmod((color.g - color.b) / delta, 6.0);
    else if (high == color.g) hue = (color.b - color.r) / delta + 2.0;
    else hue = (color.r - color.g) / delta + 4.0;
    hue /= 6.0;
    if (hue < 0.0) hue += 1.0;
  }
  return float3(hue, saturation, luminance);
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  float3 rgb = Source.Load(int3(id.xy, 0)).rgb;
  float matte = 0.0;
  if (all(isfinite(rgb))) {
    float3 hsl = rgb_to_hsl(rgb);
    matte = hue_weight(hsl.x, hue_range) *
            linear_weight(hsl.y, saturation_range) *
            linear_weight(hsl.z, luminance_range);
    if (matte <= clean_black) matte = 0.0;
    if (matte >= 1.0 - clean_white) matte = 1.0;
    if (invert != 0) matte = 1.0 - matte;
  }
  Matte[id.xy] = saturate(matte);
}
