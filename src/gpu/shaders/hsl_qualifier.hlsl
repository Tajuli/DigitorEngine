// DigitorEngine v5.0.0 HSL Qualifier native compute contract.
// The same source is compiled for Vulkan SPIR-V and D3D12 DXIL. Metal and
// OpenGL ES backends must implement the identical parameter schema and math.

struct QualifierRange {
  float low;
  float high;
  float softness;
  float padding;
};

cbuffer HslQualifierParameters : register(b0) {
  QualifierRange hue_range;
  QualifierRange saturation_range;
  QualifierRange luminance_range;
  float clean_black;
  float clean_white;
  uint invert_matte;
  uint pixel_count;
};

StructuredBuffer<float4> source_pixels : register(t0);
RWStructuredBuffer<float> destination_matte : register(u0);

float linear_weight(float value, QualifierRange range) {
  if (value >= range.low && value <= range.high) return 1.0;
  if (range.softness > 0.0 && value < range.low &&
      value > range.low - range.softness) {
    return (value - range.low + range.softness) / range.softness;
  }
  if (range.softness > 0.0 && value > range.high &&
      value < range.high + range.softness) {
    return (range.high + range.softness - value) / range.softness;
  }
  return 0.0;
}

float hue_weight(float hue, QualifierRange range) {
  if (range.low <= range.high) return linear_weight(hue, range);
  QualifierRange upper = range;
  upper.high = 1.0;
  QualifierRange lower = range;
  lower.low = 0.0;
  return max(linear_weight(hue, upper), linear_weight(hue, lower));
}

float3 rgb_to_hsl(float3 color) {
  const float high = max(color.r, max(color.g, color.b));
  const float low = min(color.r, min(color.g, color.b));
  const float delta = high - low;
  const float luminance = (high + low) * 0.5;
  const float saturation = delta == 0.0
      ? 0.0
      : delta / max(1.0e-8, 1.0 - abs(2.0 * luminance - 1.0));
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

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
  const uint index = dispatch_id.x;
  if (index >= pixel_count) return;

  const float3 rgb = source_pixels[index].rgb;
  if (!all(isfinite(rgb))) {
    destination_matte[index] = 0.0;
    return;
  }

  const float3 hsl = rgb_to_hsl(rgb);
  float matte = hue_weight(hsl.x, hue_range) *
                linear_weight(hsl.y, saturation_range) *
                linear_weight(hsl.z, luminance_range);
  if (matte <= clean_black) matte = 0.0;
  if (matte >= 1.0 - clean_white) matte = 1.0;
  destination_matte[index] = invert_matte != 0 ? 1.0 - matte : matte;
}
