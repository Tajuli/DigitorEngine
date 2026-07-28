StructuredBuffer<float4> input_buffer : register(t0); RWStructuredBuffer<float4> output_buffer : register(u0);
[numthreads(64,1,1)] void main(uint3 id:SV_DispatchThreadID) { output_buffer[id.x]=input_buffer[id.x]; }
