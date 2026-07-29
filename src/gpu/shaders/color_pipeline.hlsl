// Canonical v4.6 color math. Backend source generation must preserve these
// statements and their order; src/gpu/color.cpp is the authoritative reference.
struct Parameters {
  float exposure, contrast, gamma_, lift, gain, offset_;
  float temperature, tint, saturation, vibrance, hue;
  uint count;
};
#ifdef DIGITOR_VULKAN
[[vk::binding(0,0)]] StructuredBuffer<float4> input_pixels : register(t0);
[[vk::binding(1,0)]] RWStructuredBuffer<float4> output_pixels : register(u0);
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;
#else
StructuredBuffer<float4> input_pixels : register(t0);
RWStructuredBuffer<float4> output_pixels : register(u0);
ConstantBuffer<Parameters> parameters : register(b0);
#endif

[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
  uint k = id.x;
  if (k >= parameters.count) return;
  float4 c = input_pixels[k];
  float3 x = c.rgb;
  float temperature = parameters.temperature * .1f;
  float tint = parameters.tint * .1f;
  x[0] += temperature; x[2] -= temperature; x[1] += tint;
  float luminance = .2126f*x[0] + .7152f*x[1] + .0722f*x[2];
  float maximum = max(x[0], max(x[1], x[2]));
  float minimum = min(x[0], min(x[1], x[2]));
  float vibrance = 1 + parameters.vibrance * (1 - (maximum - minimum));
  float saturation = parameters.saturation * vibrance;
  [unroll] for (uint channel = 0; channel < 3; ++channel) {
    x[channel] = luminance + (x[channel] - luminance) * saturation;
    x[channel] = (x[channel] - .5f) * parameters.contrast + .5f;
    x[channel] = (x[channel] + parameters.lift) * parameters.gain + parameters.offset_;
    x[channel] *= exp2(parameters.exposure);
    x[channel] = sign(x[channel]) * pow(abs(x[channel]), 1 / max(.001f, parameters.gamma_));
  }
  float angle = parameters.hue * 3.1415926535f / 180;
  float co = cos(angle), s = sin(angle), r=x[0], g=x[1], b=x[2];
  x[0]=(.213f+co*.787f-s*.213f)*r+(.715f-co*.715f-s*.715f)*g+(.072f-co*.072f+s*.928f)*b;
  x[1]=(.213f-co*.213f+s*.143f)*r+(.715f+co*.285f+s*.140f)*g+(.072f-co*.072f-s*.283f)*b;
  x[2]=(.213f-co*.213f-s*.787f)*r+(.715f-co*.715f+s*.715f)*g+(.072f+co*.928f+s*.072f)*b;
  output_pixels[k] = float4(x, c.a);
}
