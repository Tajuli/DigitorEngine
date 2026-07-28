Texture2D<float4> source_texture : register(t0); SamplerState source_sampler : register(s1);
float4 main(float2 uv:TEXCOORD0):SV_Target { return source_texture.Sample(source_sampler,uv); }
