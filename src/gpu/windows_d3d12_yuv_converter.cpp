#include "digitor/windows_d3d12_yuv_converter.hpp"
#include "digitor/windows_zero_copy.hpp"

#include <atomic>
#include <cstring>
#include <new>
#include <string>
#include <utility>

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

constexpr const char kYuvShader[] = R"HLSL(
cbuffer DigitorYuvConstants : register(b0) {
  float y_offset; float y_scale; float uv_offset; float uv_scale;
  float3 row_r; float _pad0; float3 row_g; float _pad1;
  float3 row_b; float output_scale;
  uint width; uint height; uint bit_depth; uint full_range;
};
Texture2D<float> y_plane : register(t0);
Texture2D<float2> uv_plane : register(t1);
RWTexture2D<float4> output_rgba : register(u0);
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  uint2 p = id.xy; uint2 c = p >> 1;
  float y = (y_plane.Load(int3(p,0)) - y_offset) * y_scale;
  float2 uv = (uv_plane.Load(int3(c,0)) - uv_offset) * uv_scale;
  float3 yuv = float3(y, uv.x, uv.y);
  float3 rgb = float3(dot(row_r,yuv),dot(row_g,yuv),dot(row_b,yuv))*output_scale;
  output_rgba[p] = float4(rgb,1.0);
}
)HLSL";

struct Constants {
  float y_offset, y_scale, uv_offset, uv_scale;
  float row_r[3], pad0;
  float row_g[3], pad1;
  float row_b[3], output_scale;
  std::uint32_t width, height, bit_depth, full_range;
};
static_assert((sizeof(Constants) % 16) == 0);

struct Owner {
  ComPtr<ID3D12Resource> input;
  ComPtr<ID3D12Resource> output;
  NativeMediaSurfacePtr decoder_lifetime;
};

DigitorResult from_hr(HRESULT hr) noexcept {
  if (SUCCEEDED(hr)) return DIGITOR_RESULT_OK;
  return hr == E_OUTOFMEMORY ? DIGITOR_RESULT_OUT_OF_MEMORY
                              : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
}

Constants make_constants(const WindowsZeroCopySurface& s) {
  const auto c = make_windows_yuv_constants(s.format, s.color);
  Constants out{};
  out.y_offset=c.y_offset; out.y_scale=c.y_scale;
  out.uv_offset=c.uv_offset; out.uv_scale=c.uv_scale;
  std::memcpy(out.row_r,c.row_r,sizeof(out.row_r));
  std::memcpy(out.row_g,c.row_g,sizeof(out.row_g));
  std::memcpy(out.row_b,c.row_b,sizeof(out.row_b));
  out.output_scale=c.output_scale;
  out.width=s.width; out.height=s.height;
  out.bit_depth=s.format==WindowsZeroCopyFormat::p010?10u:8u;
  out.full_range=s.color.full_range?1u:0u;
  return out;
}
}
#endif

struct WindowsD3D12YuvConverter::Impl {
#ifdef _WIN32
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12DescriptorHeap> heap;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> pipeline;
  ComPtr<ID3D12Fence> fence;
  HANDLE event_handle{};
  std::atomic_uint64_t next_identity{1};
  UINT descriptor_size{};
  ~Impl(){ if(event_handle) CloseHandle(event_handle); }
#endif
};

WindowsD3D12YuvConverter::WindowsD3D12YuvConverter(void* raw)
    : impl_(std::make_shared<Impl>()) {
  if (!raw) throw std::invalid_argument("D3D12 device is required");
#ifdef _WIN32
  impl_->device=static_cast<ID3D12Device*>(raw);
  D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
  if(FAILED(impl_->device->CreateCommandQueue(&q,IID_PPV_ARGS(&impl_->queue))) ||
     FAILED(impl_->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_PPV_ARGS(&impl_->allocator))) ||
     FAILED(impl_->device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,impl_->allocator.Get(),nullptr,IID_PPV_ARGS(&impl_->list))))
    throw std::runtime_error("cannot create D3D12 zero-copy command resources");
  impl_->list->Close();

  D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=3; hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if(FAILED(impl_->device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&impl_->heap))))
    throw std::runtime_error("cannot create D3D12 zero-copy descriptor heap");
  impl_->descriptor_size=impl_->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors=2; ranges[0].BaseShaderRegister=0;
  ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors=1; ranges[1].BaseShaderRegister=0; ranges[1].OffsetInDescriptorsFromTableStart=2;
  D3D12_ROOT_PARAMETER params[2]{};
  params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[0].Constants.Num32BitValues=sizeof(Constants)/4; params[0].Constants.ShaderRegister=0;
  params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].DescriptorTable.NumDescriptorRanges=2; params[1].DescriptorTable.pDescriptorRanges=ranges;
  D3D12_ROOT_SIGNATURE_DESC rs{}; rs.NumParameters=2; rs.pParameters=params;
  ComPtr<ID3DBlob> sig,err,shader;
  if(FAILED(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&err)) ||
     FAILED(impl_->device->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&impl_->root))) ||
     FAILED(D3DCompile(kYuvShader,sizeof(kYuvShader)-1,"yuv_to_linear_rgba.hlsl",nullptr,nullptr,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&err)))
    throw std::runtime_error("cannot compile D3D12 YUV conversion pipeline");
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature=impl_->root.Get(); pd.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
  if(FAILED(impl_->device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&impl_->pipeline))) ||
     FAILED(impl_->device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&impl_->fence))))
    throw std::runtime_error("cannot create D3D12 YUV pipeline state");
  impl_->event_handle=CreateEvent(nullptr,FALSE,FALSE,nullptr);
  if(!impl_->event_handle) throw std::runtime_error("cannot create D3D12 fence event");
#else
  (void)raw;
  throw std::runtime_error("Windows D3D12 converter is unavailable on this host");
#endif
}

WindowsD3D12YuvConverter::~WindowsD3D12YuvConverter()=default;

WindowsD3D12ConvertCallback WindowsD3D12YuvConverter::callback(){
  auto keep=impl_;
  return [keep](void* resource,const WindowsZeroCopySurface& s,ProcessedGpuFramePtr& out) noexcept {
    WindowsD3D12YuvConverter wrapper(reinterpret_cast<void*>(1));
    wrapper.impl_=keep;
    return wrapper.convert(resource,s,out);
  };
}

DigitorResult WindowsD3D12YuvConverter::convert(void* raw,
    const WindowsZeroCopySurface& s, ProcessedGpuFramePtr& out) noexcept {
  out.reset();
#ifndef _WIN32
  (void)raw;(void)s;return DIGITOR_RESULT_UNSUPPORTED;
#else
  if(!raw||!s.lifetime) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try {
    auto* input=static_cast<ID3D12Resource*>(raw);
    const auto in_desc=input->GetDesc();
    if(in_desc.Format!=(s.format==WindowsZeroCopyFormat::nv12?DXGI_FORMAT_NV12:DXGI_FORMAT_P010))
      return DIGITOR_RESULT_INVALID_ARGUMENT;

    D3D12_RESOURCE_DESC od{}; od.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    od.Width=s.width; od.Height=s.height; od.DepthOrArraySize=1; od.MipLevels=1;
    od.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; od.SampleDesc.Count=1;
    od.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN; od.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> output;
    auto hr=impl_->device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&od,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output));
    if(FAILED(hr)) return from_hr(hr);

    auto cpu=impl_->heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC y{}; y.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    y.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    y.Format=s.format==WindowsZeroCopyFormat::nv12?DXGI_FORMAT_R8_UNORM:DXGI_FORMAT_R16_UNORM;
    y.Texture2D.PlaneSlice=0; impl_->device->CreateShaderResourceView(input,&y,cpu);
    cpu.ptr+=impl_->descriptor_size;
    D3D12_SHADER_RESOURCE_VIEW_DESC uv=y;
    uv.Format=s.format==WindowsZeroCopyFormat::nv12?DXGI_FORMAT_R8G8_UNORM:DXGI_FORMAT_R16G16_UNORM;
    uv.Texture2D.PlaneSlice=1; impl_->device->CreateShaderResourceView(input,&uv,cpu);
    cpu.ptr+=impl_->descriptor_size;
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D; u.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    impl_->device->CreateUnorderedAccessView(output.Get(),nullptr,&u,cpu);

    if(FAILED(impl_->allocator->Reset()) || FAILED(impl_->list->Reset(impl_->allocator.Get(),impl_->pipeline.Get())))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    ID3D12DescriptorHeap* heaps[]{impl_->heap.Get()}; impl_->list->SetDescriptorHeaps(1,heaps);
    impl_->list->SetComputeRootSignature(impl_->root.Get());
    const auto constants=make_constants(s);
    impl_->list->SetComputeRoot32BitConstants(0,sizeof(Constants)/4,&constants,0);
    impl_->list->SetComputeRootDescriptorTable(1,impl_->heap->GetGPUDescriptorHandleForHeapStart());
    impl_->list->Dispatch((s.width+7)/8,(s.height+7)/8,1);
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=output.Get(); barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    impl_->list->ResourceBarrier(1,&barrier);
    if(FAILED(impl_->list->Close())) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    ID3D12CommandList* lists[]{impl_->list.Get()}; impl_->queue->ExecuteCommandLists(1,lists);
    const auto fence_value=impl_->next_identity.fetch_add(1);
    if(FAILED(impl_->queue->Signal(impl_->fence.Get(),fence_value))) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    if(impl_->fence->GetCompletedValue()<fence_value){
      if(FAILED(impl_->fence->SetEventOnCompletion(fence_value,impl_->event_handle))) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      WaitForSingleObject(impl_->event_handle,INFINITE);
    }

    auto owner=std::make_shared<Owner>(); owner->input=input; owner->output=output; owner->decoder_lifetime=s.lifetime;
    auto ready=std::make_shared<std::atomic_bool>(true);
    GpuFrameMetadata metadata{}; metadata.width=s.width; metadata.height=s.height;
    metadata.format=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT; metadata.timestamp=s.timestamp_us;
    metadata.color_metadata="linear-rgba16f:"+std::to_string(static_cast<unsigned>(s.color.matrix))+":"+
      std::to_string(s.color.full_range?1:0)+":"+std::to_string(s.color.primaries)+":"+std::to_string(s.color.transfer);
    out=std::make_shared<ProcessedGpuFrame>(impl_->device.Get(),DIGITOR_RENDERER_D3D12,
      std::move(metadata),fence_value,owner,ready,false);
    return DIGITOR_RESULT_OK;
  } catch(const std::bad_alloc&){ return DIGITOR_RESULT_OUT_OF_MEMORY; }
    catch(...){ out.reset(); return DIGITOR_RESULT_INTERNAL_ERROR; }
#endif
}

} // namespace digitor
