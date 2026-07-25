#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstring>
#include <algorithm>
#include <vector>
#include <cstdlib>
#include "gpu/gpu_backend.hpp"
namespace digitor { namespace {
struct D3DObject { ID3D12Resource* resource{}; };
class D3DBackend final : public IRenderBackend {
 ID3D12Device* device_{};
 ID3D12CommandQueue* queue_{};
 ID3D12CommandAllocator* allocator_{};
 ID3D12GraphicsCommandList* list_{};
 ID3D12Fence* fence_{};
 HANDLE fence_event_{};
 UINT64 fence_value_{};
 ID3D12RootSignature* root_signature_{};
 ID3D12DescriptorHeap* srv_heap_{};
 ID3D12DescriptorHeap* rtv_heap_{};
 DigitorRendererInfo info_{};
 static DXGI_FORMAT format(DigitorPixelFormat f) { switch(f) { case DIGITOR_PIXEL_FORMAT_RGBA8_UNORM:return DXGI_FORMAT_R8G8B8A8_UNORM; case DIGITOR_PIXEL_FORMAT_BGRA8_UNORM:return DXGI_FORMAT_B8G8R8A8_UNORM; case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT:return DXGI_FORMAT_R16G16B16A16_FLOAT; case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT:return DXGI_FORMAT_R32G32B32A32_FLOAT; default:return DXGI_FORMAT_UNKNOWN; } }
 static DigitorResult result(HRESULT h) { return h==E_OUTOFMEMORY?DIGITOR_RESULT_OUT_OF_MEMORY:(FAILED(h)?DIGITOR_RESULT_BACKEND_UNAVAILABLE:DIGITOR_RESULT_OK); }
public:
 D3DBackend(ID3D12Device* d):device_(d){ info_.backend=DIGITOR_RENDERER_D3D12; std::strcpy(info_.backend_name,"Direct3D 12"); std::strcpy(info_.device_name,"D3D12 Adapter"); info_.is_gpu=info_.supports_compute=info_.supports_fp16=info_.supports_fp32=1; }
 ~D3DBackend(){ shutdown(); if(device_)device_->Release(); }
 bool initialize(bool enable_validation) override {
  (void)enable_validation; D3D12_COMMAND_QUEUE_DESC q{};
  if(FAILED(device_->CreateCommandQueue(&q,IID_PPV_ARGS(&queue_))) || FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator_))) || FAILED(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator_,nullptr,IID_PPV_ARGS(&list_))) || FAILED(device_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)))) return false;
  list_->Close(); fence_event_=CreateEvent(nullptr,FALSE,FALSE,nullptr); if(!fence_event_)return false;
  D3D12_ROOT_SIGNATURE_DESC rs{};rs.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;ID3DBlob* blob=nullptr;ID3DBlob* error=nullptr;
  if(FAILED(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))) {if(error)error->Release();return false;} if(error)error->Release();
  auto hr=device_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root_signature_));blob->Release();if(FAILED(hr))return false;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(FAILED(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srv_heap_))))return false;
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;return SUCCEEDED(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv_heap_)));
 }
 void shutdown()noexcept override { wait(); if(rtv_heap_){rtv_heap_->Release();rtv_heap_=nullptr;}if(srv_heap_){srv_heap_->Release();srv_heap_=nullptr;}if(root_signature_){root_signature_->Release();root_signature_=nullptr;}if(fence_){fence_->Release();fence_=nullptr;}if(list_){list_->Release();list_=nullptr;}if(allocator_){allocator_->Release();allocator_=nullptr;}if(queue_){queue_->Release();queue_=nullptr;}if(fence_event_){CloseHandle(fence_event_);fence_event_=nullptr;} }
 DigitorRendererInfo info()const noexcept override{return info_;}
 void wait() noexcept {if(!queue_||!fence_)return;const auto value=++fence_value_;if(FAILED(queue_->Signal(fence_,value)))return;if(fence_->GetCompletedValue()<value){if(SUCCEEDED(fence_->SetEventOnCompletion(value,fence_event_)))WaitForSingleObject(fence_event_,INFINITE);}}
 DigitorResult render_rgba8(uint32_t width,uint32_t height,std::span<const uint8_t> source,std::vector<uint8_t>& destination)noexcept override {
  if(!width||!height||(!source.empty()&&source.size()!=size_t(width)*height*4))return DIGITOR_RESULT_INVALID_ARGUMENT;
  ID3D12Resource *target=nullptr,*upload=nullptr,*readback=nullptr;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=width;td.Height=height;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;if(FAILED(device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&target))))return DIGITOR_RESULT_OUT_OF_MEMORY;
  UINT64 total=0,row=0;device_->GetCopyableFootprints(&td,0,1,0,nullptr,nullptr,&row,&total);D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  hp.Type=D3D12_HEAP_TYPE_UPLOAD;if(FAILED(device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload))))goto fail;
  hp.Type=D3D12_HEAP_TYPE_READBACK;if(FAILED(device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))))goto fail;
  {void* mapped=nullptr;D3D12_RANGE empty{0,0};if(FAILED(upload->Map(0,&empty,&mapped)))goto fail;for(UINT y=0;y<height;++y){auto* dst=static_cast<uint8_t*>(mapped)+y*row;if(source.empty()){for(UINT x=0;x<width;++x){dst[x*4]=dst[x*4+1]=dst[x*4+2]=0;dst[x*4+3]=255;}}else std::memcpy(dst,source.data()+size_t(y)*width*4,size_t(width)*4);}upload->Unmap(0,nullptr);}
  if(FAILED(allocator_->Reset())||FAILED(list_->Reset(allocator_,nullptr)))goto fail;D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};device_->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,nullptr);D3D12_TEXTURE_COPY_LOCATION src{upload,D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};src.PlacedFootprint=fp;D3D12_TEXTURE_COPY_LOCATION dst{target,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};list_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={target,0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};list_->ResourceBarrier(1,&barrier);D3D12_TEXTURE_COPY_LOCATION rb{readback,D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};rb.PlacedFootprint=fp;list_->CopyTextureRegion(&rb,0,0,0,&dst,nullptr);if(FAILED(list_->Close()))goto fail;{ID3D12CommandList* lists[]={list_};queue_->ExecuteCommandLists(1,lists);}wait();
  try{destination.resize(size_t(width)*height*4);}catch(...){goto fail;} {void* mapped=nullptr;D3D12_RANGE range{0,total};if(FAILED(readback->Map(0,&range,&mapped)))goto fail;for(UINT y=0;y<height;++y)std::memcpy(destination.data()+size_t(y)*width*4,static_cast<uint8_t*>(mapped)+y*row,size_t(width)*4);D3D12_RANGE written{0,0};readback->Unmap(0,&written);} readback->Release();upload->Release();target->Release();return DIGITOR_RESULT_OK;
 fail: if(readback)readback->Release();if(upload)upload->Release();if(target)target->Release();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
 }
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
if(b!=DIGITOR_RENDERER_D3D12)return nullptr;if(std::getenv("DIGITOR_GPU_VALIDATION")){ID3D12Debug* debug=nullptr;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))){debug->EnableDebugLayer();debug->Release();}}ID3D12Device*d=nullptr;if(FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d))))return nullptr;return std::make_unique<D3DBackend>(d);}
}
#endif
