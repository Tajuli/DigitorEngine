Texture2D<float4> Original : register(t0); Texture2D<float4> Processed : register(t1); Texture2D<float> Matte : register(t2); RWTexture2D<float4> Output : register(u0);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){float m=saturate(Matte[id.xy]);Output[id.xy]=lerp(Original[id.xy],Processed[id.xy],m);}
