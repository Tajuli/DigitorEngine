#pragma once
namespace digitor { inline constexpr char digitor_log_wheels_hlsl[] = R"hlsl(// Digitor independently designed deterministic Log Wheels FP32 shader ABI v1.
#ifdef DIGITOR_TEXTURE_OUTPUT
#ifdef DIGITOR_VULKAN
[[vk::binding(0,0)]] Texture2D<float4> sourceTexture : register(t0);
[[vk::binding(1,0)]] RWTexture2D<float4> destinationTexture : register(u0);
[[vk::binding(2,0)]] cbuffer Parameters : register(b0) { float4 shadows; float4 midtones; float4 highlights; float4 globalWheel; uint4 enabled; float4 tonal; uint pixelCount; uint imageWidth; uint imageHeight; uint padding0; };
#else
Texture2D<float4> sourceTexture : register(t0); RWTexture2D<float4> destinationTexture : register(u0);
cbuffer Parameters : register(b0) { float4 shadows; float4 midtones; float4 highlights; float4 globalWheel; uint4 enabled; float4 tonal; uint pixelCount; uint imageWidth; uint imageHeight; uint padding0; };
#endif
#else
StructuredBuffer<float4> sourcePixels : register(t0); RWStructuredBuffer<float4> destinationPixels : register(u0);
cbuffer Parameters : register(b0) { float4 shadows; float4 midtones; float4 highlights; float4 globalWheel; uint4 enabled; float4 tonal; uint pixelCount; uint imageWidth; uint imageHeight; uint padding0; };
#endif
float smoothBand(float a,float b,float x){float t=saturate((x-a)/(b-a));return t*t*(3.0-2.0*t);}
[numthreads(64,1,1)] void main(uint3 id:SV_DispatchThreadID){
 if(id.x>=pixelCount)return;
#ifdef DIGITOR_TEXTURE_OUTPUT
 uint2 coordinate=uint2(id.x%imageWidth,id.x/imageWidth);float4 c=sourceTexture.Load(int3(coordinate,0));
#else
 float4 c=sourcePixels[id.x];
#endif
 float alpha=c.a;float y=dot(c.rgb,float3(0.2126,0.7152,0.0722));float halfWidth=tonal.z*0.5;
 float sw=1.0-smoothBand(tonal.x-halfWidth,tonal.x+halfWidth,y);
 float hw=smoothBand(tonal.y-halfWidth,tonal.y+halfWidth,y);float mw=max(0.0,1.0-sw-hw);
 float stop=(enabled.x?shadows.a*sw:0)+(enabled.y?midtones.a*mw:0)+(enabled.z?highlights.a*hw:0)+(enabled.w?globalWheel.a:0);
 float3 balance=(enabled.x?shadows.rgb*sw:0)+(enabled.y?midtones.rgb*mw:0)+(enabled.z?highlights.rgb*hw:0)+(enabled.w?globalWheel.rgb:0);
 c.rgb=c.rgb*exp2(stop)+balance;c.a=alpha;
#ifdef DIGITOR_TEXTURE_OUTPUT
 destinationTexture[coordinate]=c;
#else
 destinationPixels[id.x]=c;
#endif
}
)hlsl"; }
