// Additive GPU-first YUV conversion kernel for imported decoder surfaces.
// Preview and export must bind the same constants and shader bytecode.

cbuffer DigitorYuvConstants : register(b0) {
  float y_offset;
  float y_scale;
  float uv_offset;
  float uv_scale;
  float3 row_r;
  float3 row_g;
  float3 row_b;
  float output_scale;
  uint width;
  uint height;
  uint bit_depth;
  uint full_range;
};

Texture2D<float> y_plane : register(t0);
Texture2D<float2> uv_plane : register(t1);
RWTexture2D<float4> output_rgba : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
  if (dispatch_id.x >= width || dispatch_id.y >= height) return;

  const uint2 pixel = dispatch_id.xy;
  const uint2 chroma_pixel = pixel >> 1;

  const float encoded_y = y_plane.Load(int3(pixel, 0));
  const float2 encoded_uv = uv_plane.Load(int3(chroma_pixel, 0));

  const float y = (encoded_y - y_offset) * y_scale;
  const float cb = (encoded_uv.x - uv_offset) * uv_scale;
  const float cr = (encoded_uv.y - uv_offset) * uv_scale;
  const float3 yuv = float3(y, cb, cr);

  // Do not saturate here. Super-white and sub-black values remain available to
  // the grading pipeline and are clipped only by an explicit output transform.
  const float3 rgb = float3(dot(row_r, yuv),
                            dot(row_g, yuv),
                            dot(row_b, yuv)) * output_scale;
  output_rgba[pixel] = float4(rgb, 1.0f);
}
