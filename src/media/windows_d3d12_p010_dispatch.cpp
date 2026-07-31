#include "digitor/windows_d3d12_p010_dispatch.hpp"

#include <mutex>
#include <new>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#endif

namespace digitor {

struct WindowsD3D12P010Dispatch::Impl {
  WindowsD3D12P010DispatchConfig config;
  mutable std::mutex mutex;
  WindowsD3D12P010DispatchTelemetry telemetry;
#ifdef _WIN32
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
#endif
};

WindowsD3D12P010Dispatch::WindowsD3D12P010Dispatch(WindowsD3D12P010DispatchConfig c)
    : impl_(std::make_shared<Impl>()) { impl_->config=std::move(c); }
WindowsD3D12P010Dispatch::~WindowsD3D12P010Dispatch()=default;

DigitorResult WindowsD3D12P010Dispatch::initialize() noexcept {
#ifndef _WIN32
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  try {
    auto& i=*impl_;
    if(!i.config.device || i.config.shader_path.empty() || i.config.descriptor_count<3)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    i.device=static_cast<ID3D12Device*>(i.config.device);

    D3D12_FEATURE_DATA_FORMAT_SUPPORT y{}; y.Format=DXGI_FORMAT_R16_UNORM;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT uv{}; uv.Format=DXGI_FORMAT_R16G16_UNORM;
    if(FAILED(i.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&y,sizeof(y))) ||
       FAILED(i.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&uv,sizeof(uv))) ||
       !(y.Support1&D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) ||
       !(uv.Support1&D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW)) {
      std::scoped_lock lock(i.mutex);++i.telemetry.unsupported_format_failures;
      i.telemetry.diagnostic="P010 plane typed UAV support is unavailable";
      return DIGITOR_RESULT_UNSUPPORTED;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const auto hr=D3DCompileFromFile(std::wstring(i.config.shader_path.begin(),i.config.shader_path.end()).c_str(),
        nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,i.config.entry_point.c_str(),"cs_5_1",
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors);
    if(FAILED(hr)) {
      std::scoped_lock lock(i.mutex);++i.telemetry.shader_compile_failures;
      i.telemetry.diagnostic=errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"P010 shader compilation failed";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=1;ranges[0].BaseShaderRegister=0;
    ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=2;ranges[1].BaseShaderRegister=0;
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={1,&ranges[0]};
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={1,&ranges[1]};
    params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={0,0,sizeof(WindowsP010GpuConstants)/4};
    D3D12_ROOT_SIGNATURE_DESC rs{};rs.NumParameters=3;rs.pParameters=params;
    Microsoft::WRL::ComPtr<ID3DBlob> rs_blob,rs_error;
    if(FAILED(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&rs_blob,&rs_error)) ||
       FAILED(i.device->CreateRootSignature(0,rs_blob->GetBufferPointer(),rs_blob->GetBufferSize(),IID_PPV_ARGS(&i.root_signature))))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=i.root_signature.Get();pso.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
    if(FAILED(i.device->CreateComputePipelineState(&pso,IID_PPV_ARGS(&i.pipeline)))) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=i.config.descriptor_count;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if(FAILED(i.device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&i.descriptor_heap))) ||
       FAILED(i.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_PPV_ARGS(&i.allocator))) ||
       FAILED(i.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,i.allocator.Get(),i.pipeline.Get(),IID_PPV_ARGS(&i.command_list))))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    i.command_list->Close();
    std::scoped_lock lock(i.mutex);i.telemetry.diagnostic="D3D12 P010 dispatch initialized";return DIGITOR_RESULT_OK;
  } catch(const std::bad_alloc&) { return DIGITOR_RESULT_OUT_OF_MEMORY; }
    catch(...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
#endif
}

DigitorResult WindowsD3D12P010Dispatch::dispatch(void* rgba,void* p010,const WindowsP010GpuConstants& c,
    void* queue,void* fence,std::uint64_t fence_value) noexcept {
#ifndef _WIN32
  (void)rgba;(void)p010;(void)c;(void)queue;(void)fence;(void)fence_value;return DIGITOR_RESULT_UNSUPPORTED;
#else
  auto& i=*impl_;
  if(!rgba||!p010||!queue||!fence||!i.pipeline||!i.root_signature) return DIGITOR_RESULT_INVALID_ARGUMENT;
  auto* src=static_cast<ID3D12Resource*>(rgba);auto* dst=static_cast<ID3D12Resource*>(p010);
  if(src->GetDesc().Format!=DXGI_FORMAT_R16G16B16A16_FLOAT || dst->GetDesc().Format!=DXGI_FORMAT_P010)
    return DIGITOR_RESULT_UNSUPPORTED;
  try {
    if(FAILED(i.allocator->Reset()) || FAILED(i.command_list->Reset(i.allocator.Get(),i.pipeline.Get()))) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barriers[0].Transition={src,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    barriers[1].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barriers[1].Transition={dst,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    i.command_list->ResourceBarrier(2,barriers);
    const auto inc=i.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu=i.descriptor_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    i.device->CreateShaderResourceView(src,&srv,cpu);
    cpu.ptr+=inc;D3D12_UNORDERED_ACCESS_VIEW_DESC y{};y.Format=DXGI_FORMAT_R16_UNORM;y.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;y.Texture2D.PlaneSlice=0;i.device->CreateUnorderedAccessView(dst,nullptr,&y,cpu);
    cpu.ptr+=inc;D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=DXGI_FORMAT_R16G16_UNORM;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;uv.Texture2D.PlaneSlice=1;i.device->CreateUnorderedAccessView(dst,nullptr,&uv,cpu);
    ID3D12DescriptorHeap* heaps[]{i.descriptor_heap.Get()};i.command_list->SetDescriptorHeaps(1,heaps);i.command_list->SetComputeRootSignature(i.root_signature.Get());
    auto gpu=i.descriptor_heap->GetGPUDescriptorHandleForHeapStart();i.command_list->SetComputeRootDescriptorTable(0,gpu);gpu.ptr+=inc;i.command_list->SetComputeRootDescriptorTable(1,gpu);
    i.command_list->SetComputeRoot32BitConstants(2,sizeof(c)/4,&c,0);i.command_list->Dispatch((c.width+15)/16,(c.height+15)/16,1);
    std::swap(barriers[0].Transition.StateBefore,barriers[0].Transition.StateAfter);std::swap(barriers[1].Transition.StateBefore,barriers[1].Transition.StateAfter);i.command_list->ResourceBarrier(2,barriers);
    if(FAILED(i.command_list->Close())) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    ID3D12CommandList* lists[]{i.command_list.Get()};auto* q=static_cast<ID3D12CommandQueue*>(queue);q->ExecuteCommandLists(1,lists);
    if(FAILED(q->Signal(static_cast<ID3D12Fence*>(fence),fence_value))) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    std::scoped_lock lock(i.mutex);++i.telemetry.submissions;++i.telemetry.completed;i.telemetry.diagnostic="P010 GPU dispatch completed";return DIGITOR_RESULT_OK;
  } catch(...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
#endif
}

WindowsP010GpuDispatch WindowsD3D12P010Dispatch::callback(){auto keep=impl_;return [keep](void* a,void* b,const WindowsP010GpuConstants& c,void* q,void* f,std::uint64_t v) noexcept {WindowsD3D12P010Dispatch d(keep->config);d.impl_=keep;return d.dispatch(a,b,c,q,f,v);};}
WindowsD3D12P010DispatchTelemetry WindowsD3D12P010Dispatch::telemetry() const {std::scoped_lock lock(impl_->mutex);return impl_->telemetry;}
bool WindowsD3D12P010Dispatch::gpu_only() const noexcept {std::scoped_lock lock(impl_->mutex);return impl_->telemetry.cpu_copies==0;}

} // namespace digitor
