#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstring>
#include "gpu/gpu_backend.hpp"
namespace digitor { namespace {
struct D3DObject { ID3D12Resource* resource{}; };
class D3DBackend final : public IRenderBackend {
 ID3D12Device* device_{}; DigitorRendererInfo info_{};
 static DXGI_FORMAT format(DigitorPixelFormat f) { switch(f) { case DIGITOR_PIXEL_FORMAT_RGBA8_UNORM:return DXGI_FORMAT_R8G8B8A8_UNORM; case DIGITOR_PIXEL_FORMAT_BGRA8_UNORM:return DXGI_FORMAT_B8G8R8A8_UNORM; case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT:return DXGI_FORMAT_R16G16B16A16_FLOAT; case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT:return DXGI_FORMAT_R32G32B32A32_FLOAT; default:return DXGI_FORMAT_UNKNOWN; } }
 static DigitorResult result(HRESULT h) { return h==E_OUTOFMEMORY?DIGITOR_RESULT_OUT_OF_MEMORY:(FAILED(h)?DIGITOR_RESULT_BACKEND_UNAVAILABLE:DIGITOR_RESULT_OK); }
public:
 D3DBackend(ID3D12Device* d):device_(d){ info_.backend=DIGITOR_RENDERER_D3D12; std::strcpy(info_.backend_name,"Direct3D 12"); std::strcpy(info_.device_name,"D3D12 Adapter"); info_.is_gpu=info_.supports_compute=info_.supports_fp16=info_.supports_fp32=1; }
 ~D3DBackend(){ if(device_)device_->Release(); } bool initialize(bool)override{return true;} void shutdown()noexcept override{} DigitorRendererInfo info()const noexcept override{return info_;}
 DigitorResult create_texture(const DigitorTextureDesc& d,void** out)noexcept override { *out=nullptr; D3D12_RESOURCE_DESC rd{}; rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=d.width;rd.Height=d.height;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.Format=format(d.format);rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN; if(d.usage&DIGITOR_TEXTURE_USAGE_RENDER_TARGET)rd.Flags|=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;if(d.usage&DIGITOR_TEXTURE_USAGE_STORAGE)rd.Flags|=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;auto* o=new(std::nothrow)D3DObject;if(!o)return DIGITOR_RESULT_OUT_OF_MEMORY; auto h=device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&o->resource));if(FAILED(h)){delete o;return result(h);}*out=o;return DIGITOR_RESULT_OK; }
 DigitorResult create_buffer(const DigitorBufferDesc& d,void** out)noexcept override { *out=nullptr; D3D12_HEAP_PROPERTIES hp{};hp.Type=(d.usage&(DIGITOR_BUFFER_USAGE_UPLOAD|DIGITOR_BUFFER_USAGE_STAGING))?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=d.size;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;if(d.usage&DIGITOR_BUFFER_USAGE_STORAGE)rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;auto*o=new(std::nothrow)D3DObject;if(!o)return DIGITOR_RESULT_OUT_OF_MEMORY;auto state=hp.Type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COMMON;auto h=device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(&o->resource));if(FAILED(h)){delete o;return result(h);}*out=o;return DIGITOR_RESULT_OK; }
 DigitorResult create_sampler(const DigitorSamplerDesc& d,void**out)noexcept override { auto*p=new(std::nothrow)DigitorSamplerDesc(d);*out=p;return p?DIGITOR_RESULT_OK:DIGITOR_RESULT_OUT_OF_MEMORY; }
 DigitorResult map_buffer(void* p,uint64_t offset,uint64_t size,void**out)noexcept override { if(!p||!out)return DIGITOR_RESULT_INVALID_ARGUMENT;*out=nullptr;auto*o=static_cast<D3DObject*>(p);D3D12_RANGE readRange{0,0};void*base=nullptr;auto h=o->resource->Map(0,&readRange,&base);if(FAILED(h))return result(h);*out=static_cast<unsigned char*>(base)+offset;(void)size;return DIGITOR_RESULT_OK; }
 void unmap_buffer(void*p)noexcept override {if(p)static_cast<D3DObject*>(p)->resource->Unmap(0,nullptr);}
 void destroy_texture(void*p)noexcept override{destroy(p);}void destroy_buffer(void*p)noexcept override{destroy(p);}void destroy_sampler(void*p)noexcept override{delete static_cast<DigitorSamplerDesc*>(p);} static void destroy(void*p){auto*o=static_cast<D3DObject*>(p);if(o){if(o->resource)o->resource->Release();delete o;}}
}; }
std::unique_ptr<IRenderBackend> create_native_backend(DigitorRendererBackend b){
#ifdef DIGITOR_HAS_VULKAN
extern std::unique_ptr<IRenderBackend> create_vulkan_backend(); if(b==DIGITOR_RENDERER_VULKAN)return create_vulkan_backend();
#endif
if(b!=DIGITOR_RENDERER_D3D12)return nullptr;ID3D12Device*d=nullptr;if(FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d))))return nullptr;return std::make_unique<D3DBackend>(d);}
}
#endif
