// Backend-native node-local Power Window matte. Output is single-channel R32F.
RWTexture2D<float> Matte : register(u0);
cbuffer Params : register(b0) {
  float center_x;
  float center_y;
  float window_width;
  float window_height;
  float rotation_radians;
  float feather;
  float opacity;
  uint shape;
  uint invert;
  uint width;
  uint height;
};
float feather_weight(float distance_value, float edge, float feather_amount) {
  if (feather_amount <= 0.0) return distance_value <= edge ? 1.0 : 0.0;
  return 1.0 - smoothstep(edge, edge + feather_amount, distance_value);
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
  float2 p = uv - float2(center_x, center_y);
  float c = cos(-rotation_radians);
  float s = sin(-rotation_radians);
  p = float2(c * p.x - s * p.y, s * p.x + c * p.y);
  float half_w = max(window_width * 0.5, 1e-6);
  float half_h = max(window_height * 0.5, 1e-6);
  float matte = 0.0;
  if (shape == 0u) {
    float2 q = abs(p) - float2(half_w, half_h);
    float outside_distance = length(max(q, 0.0));
    float inside_distance = min(max(q.x, q.y), 0.0);
    float signed_distance = outside_distance + inside_distance;
    matte = feather_weight(signed_distance, 0.0, feather);
  } else if (shape == 1u) {
    float ellipse_distance = length(float2(p.x / half_w, p.y / half_h));
    matte = feather_weight(ellipse_distance, 1.0, feather / max(min(half_w, half_h), 1e-6));
  } else {
    float projected = p.x / half_w;
    matte = 1.0 - smoothstep(-feather, feather, projected);
  }
  matte = saturate(matte) * saturate(opacity);
  if (invert != 0u) matte = 1.0 - matte;
  Matte[id.xy] = saturate(matte);
}
