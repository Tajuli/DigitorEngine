#include "digitor/windows_d3d12_qualified_converter.hpp"
#include "digitor/windows_zero_copy.hpp"

#include <atomic>
#include <cstring>
#include <new>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include <d3dcompiler.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace digitor {
#ifdef _WIN32
namespace {
using Microsoft::WRL::ComPtr;
constexpr char kShader[] = R"(
cbuffer C:register(b0){float yo;float ys;float uvo;float uvs;float3 rr;float p0;float3 rg;float p1;float3 rb;float os;uint w;uint h;uint bd;uint fr;};
Texture2D<float> Y:register(t0);Texture2D<float2> UV:register(t1);RWTexture2D<float4> O:register(u0);
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(id.x>=w||id.y>=h)return;float y=(Y.Load(int3(id.xy,0))-yo)*ys;float2 uv=(UV.Load(int3(id.xy>>1,0))-uvo)*uvs;float3 v=float3(y,uv);O[id.xy]=float4(dot(rr,v),dot(rg,v),dot(rb,v),1)*float4(os,os,os,1);})";
struct C {float yo,ys,uvo,uvs;float rr[3],p0,rg[3],p1,rb[3],os;std::uint32_t w,h,bd,fr;};
struct Owner {ComPtr<ID3D12Resource> input,output;NativeMediaSurfacePtr lifetime;};
DigitorResult hresult(HRESULT h){return h==E_OUTOFMEMORY?DIGITOR_RESULT_OUT_OF_MEMORY:(FAILED(h)?DIGITOR_RESULT_BACKEND_UNAVAILABLE:DIGITOR_RESULT_OK);}
C constants(const WindowsZeroCopySurface&s){const auto x=make_windows_yuv_constants(s.format,s.color);C c{};c.yo=x.y_offset;c.ys=x.y_scale;c.uvo=x.uv_offset;c.uvs=x.uv_scale;std::memcpy(c.rr,x.row_r,sizeof(c.rr));std::memcpy(c.rg,x.row_g,sizeof(c.rg));std::memcpy(c.rb,x.row_b,sizeof(c.rb));c.os=x.output_scale;c.w=s.width;c.h=s.height;c.bd=s.format==WindowsZeroCopyFormat::p010?10u:8u;c.fr=s.color.full_range;return c;}
}
#endif

struct WindowsD3D12QualifiedConverter::Impl {
#ifdef _WIN32
 ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12DescriptorHeap> heap;ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pipeline;ComPtr<ID3D12Fence> fence;HANDLE event{};UINT step{};std::atomic_uint64_t sequence{1};
 ~Impl(){if(event)CloseHandle(event);}
 DigitorResult wait(std::uint64_t value) noexcept {if(fence->GetCompletedValue()>=value)return DIGITOR_RESULT_OK;if(FAILED(fence->SetEventOnCompletion(value,event)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;return WaitForSingleObject(event,INFINITE)==WAIT_OBJECT_0?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
#endif
};

WindowsD3D12QualifiedConverter::WindowsD3D12QualifiedConverter(void* raw):impl_(std::make_shared<Impl>()){
#ifndef _WIN32
 (void)raw;throw std::runtime_error("D3D12 unavailable");
#else
 if(!raw)throw std::invalid_argument("D3D12 device required");impl_->device=static_cast<ID3D12Device*>(raw);
 D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
 if(FAILED(impl_->device->CreateCommandQueue(&q,IID_PPV_ARGS(&impl_->queue)))||FAILED(impl_->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_PPV_ARGS(&impl_->allocator)))||FAILED(impl_->device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,impl_->allocator.Get(),nullptr,IID_PPV_ARGS(&impl_->list))))throw std::runtime_error("D3D12 command creation failed");impl_->list->Close();
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=3;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(FAILED(impl_->device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&impl_->heap))))throw std::runtime_error("descriptor heap failed");impl_->step=impl_->device->GetDescriptorHandleIncrementSize(hd.Type);
 D3D12_DESCRIPTOR_RANGE ranges[2]{{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,2}};D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[0].Constants={sizeof(C)/4,0,0};p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[1].DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC rs{};rs.NumParameters=2;rs.pParameters=p;ComPtr<ID3DBlob> sig,err,shader;
 if(FAILED(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&err))||FAILED(impl_->device->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&impl_->root)))||FAILED(D3DCompile(kShader,sizeof(kShader)-1,"qualified_yuv.hlsl",nullptr,nullptr,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&err)))throw std::runtime_error("shader creation failed");D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=impl_->root.Get();pd.CS={shader->GetBufferPointer(),shader->GetBufferSize()};if(FAILED(impl_->device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&impl_->pipeline)))||FAILED(impl_->device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&impl_->fence))))throw std::runtime_error("pipeline creation failed");impl_->event=CreateEvent(nullptr,FALSE,FALSE,nullptr);if(!impl_->event)throw std::runtime_error("fence event failed");
#endif
}
WindowsD3D12QualifiedConverter::~WindowsD3D12QualifiedConverter()=default;
WindowsD3D12ConvertCallback WindowsD3D12QualifiedConverter::callback(){auto self=impl_;return [self](void*r,const WindowsZeroCopySurface&s,ProcessedGpuFramePtr&o,std::string*)noexcept{WindowsD3D12QualifiedConverter c(self);return c.convert(r,s,o);};}

DigitorResult WindowsD3D12QualifiedConverter::convert(void* raw,const WindowsZeroCopySurface&s,ProcessedGpuFramePtr&out)noexcept{
 out.reset();
#ifndef _WIN32
 (void)raw;(void)s;return DIGITOR_RESULT_UNSUPPORTED;
#else
 try{
  if(!raw||!s.lifetime)return DIGITOR_RESULT_INVALID_ARGUMENT;auto* input=static_cast<ID3D12Resource*>(raw);D3D12_RESOURCE_DESC od{};od.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;od.Width=s.width;od.Height=s.height;od.DepthOrArraySize=1;od.MipLevels=1;od.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;od.SampleDesc.Count=1;od.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> output;auto hr=impl_->device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&od,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output));if(FAILED(hr))return hresult(hr);
  auto cpu=impl_->heap->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC y{};y.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;y.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;y.Format=s.format==WindowsZeroCopyFormat::nv12?DXGI_FORMAT_R8_UNORM:DXGI_FORMAT_R16_UNORM;y.Texture2D.PlaneSlice=0;impl_->device->CreateShaderResourceView(input,&y,cpu);cpu.ptr+=impl_->step;auto uv=y;uv.Format=s.format==WindowsZeroCopyFormat::nv12?DXGI_FORMAT_R8G8_UNORM:DXGI_FORMAT_R16G16_UNORM;uv.Texture2D.PlaneSlice=1;impl_->device->CreateShaderResourceView(input,&uv,cpu);cpu.ptr+=impl_->step;D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;u.Format=od.Format;impl_->device->CreateUnorderedAccessView(output.Get(),nullptr,&u,cpu);
  if(FAILED(impl_->allocator->Reset())||FAILED(impl_->list->Reset(impl_->allocator.Get(),impl_->pipeline.Get())))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;ID3D12DescriptorHeap* heaps[]{impl_->heap.Get()};impl_->list->SetDescriptorHeaps(1,heaps);impl_->list->SetComputeRootSignature(impl_->root.Get());auto c=constants(s);impl_->list->SetComputeRoot32BitConstants(0,sizeof(C)/4,&c,0);impl_->list->SetComputeRootDescriptorTable(1,impl_->heap->GetGPUDescriptorHandleForHeapStart());impl_->list->Dispatch((s.width+7)/8,(s.height+7)/8,1);D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};impl_->list->ResourceBarrier(1,&b);if(FAILED(impl_->list->Close()))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;ID3D12CommandList* lists[]{impl_->list.Get()};impl_->queue->ExecuteCommandLists(1,lists);auto id=impl_->sequence.fetch_add(1);if(FAILED(impl_->queue->Signal(impl_->fence.Get(),id))||impl_->wait(id)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  auto owner=std::make_shared<Owner>();owner->input=input;owner->output=output;owner->lifetime=s.lifetime;auto keep=impl_;auto readback=[keep,owner,w=s.width,h=s.height](std::vector<float>&dst)noexcept->DigitorResult{try{D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows{};UINT64 row_bytes{},total{};auto desc=owner->output->GetDesc();keep->device->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&row_bytes,&total);D3D12_HEAP_PROPERTIES rh{};rh.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> rb;auto hr=keep->device->CreateCommittedResource(&rh,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb));if(FAILED(hr))return hresult(hr);if(FAILED(keep->allocator->Reset())||FAILED(keep->list->Reset(keep->allocator.Get(),nullptr)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;D3D12_TEXTURE_COPY_LOCATION d{};d.pResource=rb.Get();d.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;d.PlacedFootprint=fp;D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=owner->output.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;keep->list->CopyTextureRegion(&d,0,0,0,&src,nullptr);if(FAILED(keep->list->Close()))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;ID3D12CommandList* ls[]{keep->list.Get()};keep->queue->ExecuteCommandLists(1,ls);auto fv=keep->sequence.fetch_add(1);if(FAILED(keep->queue->Signal(keep->fence.Get(),fv))||keep->wait(fv)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;void* mapped{};D3D12_RANGE range{0,static_cast<SIZE_T>(total)};if(FAILED(rb->Map(0,&range,&mapped)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;dst.resize(static_cast<size_t>(w)*h*4);for(UINT y=0;y<h;++y){auto* row=reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(mapped)+fp.Offset+static_cast<size_t>(y)*fp.Footprint.RowPitch);for(UINT x=0;x<w*4;++x){const std::uint16_t bits=row[x];float value{};std::uint32_t sign=(bits>>15)&1,exp=(bits>>10)&31,mant=bits&1023;if(exp==0)value=std::ldexp(static_cast<float>(mant),-24);else if(exp==31)value=mant?NAN:INFINITY;else value=std::ldexp(1.0f+static_cast<float>(mant)/1024.0f,static_cast<int>(exp)-15);dst[static_cast<size_t>(y)*w*4+x]=sign?-value:value;}}rb->Unmap(0,nullptr);return DIGITOR_RESULT_OK;}catch(const std::bad_alloc&){return DIGITOR_RESULT_OUT_OF_MEMORY;}catch(...){return DIGITOR_RESULT_INTERNAL_ERROR;}};
  GpuFrameMetadata m{};m.width=s.width;m.height=s.height;m.format=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;m.timestamp=s.timestamp_us;m.color_metadata="linear-rgba16f-qualified";out=std::make_shared<ProcessedGpuFrame>(impl_->device.Get(),DIGITOR_RENDERER_D3D12,std::move(m),id,owner,std::make_shared<std::atomic_bool>(true),true,std::move(readback));return DIGITOR_RESULT_OK;
 }catch(const std::bad_alloc&){return DIGITOR_RESULT_OUT_OF_MEMORY;}catch(...){return DIGITOR_RESULT_INTERNAL_ERROR;}
#endif
}

} // namespace digitor
