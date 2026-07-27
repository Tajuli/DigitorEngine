cbuffer Parameters : register(b0) { float amount; }; Texture2D<float4> source_texture : register(t0); SamplerState source_sampler : register(s0);
float4 main(float2 uv:TEXCOORD0):SV_Target { float4 c=source_texture.Sample(source_sampler,uv); return c * amount; }
