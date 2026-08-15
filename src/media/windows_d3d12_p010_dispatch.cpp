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
namespace {
#ifdef _WIN32
constexpr char kProductionP010Shader[] = R"HLSL(
cbuffer ConversionConstants : register(b0) {
  float4 y_row;
  float4 cb_row;
  float4 cr_row;
  float y_offset;
  float y_scale;
  float uv_offset;
  float uv_scale;
  float mastering_peak_nits;
  uint width;
  uint height;
  uint transfer;
  uint flags;
};
Texture2D<float4> source_rgba : register(t0);
RWTexture2D<uint> output_y : register(u0);
RWTexture2D<uint2> output_uv : register(u1);
float pq_oetf(float linear_nits) {
  const float m1 = 2610.0 / 16384.0;
  const float m2 = 2523.0 / 32.0;
  const float c1 = 3424.0 / 4096.0;
  const float c2 = 2413.0 / 128.0;
  const float c3 = 2392.0 / 128.0;
  float l = max(linear_nits / 10000.0, 0.0);
  float p = pow(l, m1);
  return pow((c1 + c2 * p) / (1.0 + c3 * p), m2);
}
float hlg_oetf(float linear_value) {
  const float a = 0.17883277;
  const float b = 0.28466892;
  const float c = 0.55991073;
  return linear_value <= (1.0 / 12.0)
    ? sqrt(3.0 * max(linear_value, 0.0))
    : a * log(12.0 * linear_value - b) + c;
}
float encode_transfer(float value) {
  if (transfer == 2) return pq_oetf(value * mastering_peak_nits);
  if (transfer == 3) return hlg_oetf(max(value, 0.0));
  return value <= 0.0031308
    ? 12.92 * value
    : 1.055 * pow(max(value, 0.0), 1.0 / 2.4) - 0.055;
}
float3 encoded_rgb(uint2 p) {
  float3 value = source_rgba.Load(int3(p, 0)).rgb;
  if ((flags & 4u) != 0u) return saturate(value);
  return float3(encode_transfer(value.r), encode_transfer(value.g), encode_transfer(value.b));
}
uint pack_p010(float code_value) {
  float bounded = (flags & 2u) != 0u ? code_value : clamp(code_value, 0.0, 1023.0);
  return (uint)round(clamp(bounded, 0.0, 1023.0)) << 6;
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 p = id.xy;
  if (p.x >= width || p.y >= height) return;
  float3 rgb = encoded_rgb(p);
  float y = dot(y_row.xyz, rgb) + y_row.w;
  output_y[p] = pack_p010(y_offset + y * y_scale);
  if ((p.x & 1u) == 0u && (p.y & 1u) == 0u) {
    uint2 p1 = uint2(min(p.x + 1u, width - 1u), p.y);
    uint2 p2 = uint2(p.x, min(p.y + 1u, height - 1u));
    uint2 p3 = uint2(p1.x, p2.y);
    float3 average_rgb = (rgb + encoded_rgb(p1) + encoded_rgb(p2) + encoded_rgb(p3)) * 0.25;
    float cb = dot(cb_row.xyz, average_rgb) + cb_row.w;
    float cr = dot(cr_row.xyz, average_rgb) + cr_row.w;
    output_uv[p >> 1] = uint2(pack_p010(uv_offset + cb * uv_scale),
                              pack_p010(uv_offset + cr * uv_scale));
  }
}
)HLSL";

DXGI_FORMAT input_dxgi_format(DigitorPixelFormat format) noexcept {
  switch (format) {
    case DIGITOR_PIXEL_FORMAT_RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
  }
}
#endif
}  // namespace

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
    const auto source_format=input_dxgi_format(i.config.input_format);
    if(!i.config.device || source_format==DXGI_FORMAT_UNKNOWN || i.config.descriptor_count<3)
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
    HRESULT hr=E_FAIL;
    if(!i.config.shader_path.empty()) {
      hr=D3DCompileFromFile(std::wstring(i.config.shader_path.begin(),i.config.shader_path.end()).c_str(),
          nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,i.config.entry_point.c_str(),"cs_5_1",
          D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors);
    } else {
      hr=D3DCompile(kProductionP010Shader,sizeof(kProductionP010Shader)-1,
          "digitor_embedded_rgba_to_p010.hlsl",nullptr,nullptr,i.config.entry_point.c_str(),"cs_5_1",
          D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors);
    }
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
  const auto source_format=input_dxgi_format(i.config.input_format);
  if(source_format==DXGI_FORMAT_UNKNOWN || src->GetDesc().Format!=source_format ||
     dst->GetDesc().Format!=DXGI_FORMAT_P010)
    return DIGITOR_RESULT_UNSUPPORTED;
  try {
    if(FAILED(i.allocator->Reset()) || FAILED(i.command_list->Reset(i.allocator.Get(),i.pipeline.Get()))) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    D3D12_RESOURCE_BARRIER barriers[2]{};
    UINT barrier_count=0;
    if(!i.config.source_starts_shader_readable) {
      barriers[barrier_count].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barriers[barrier_count++].Transition={src,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    }
    barriers[barrier_count].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[barrier_count++].Transition={dst,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    i.command_list->ResourceBarrier(barrier_count,barriers);
    const auto inc=i.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu=i.descriptor_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=source_format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    i.device->CreateShaderResourceView(src,&srv,cpu);
    cpu.ptr+=inc;D3D12_UNORDERED_ACCESS_VIEW_DESC y{};y.Format=DXGI_FORMAT_R16_UNORM;y.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;y.Texture2D.PlaneSlice=0;i.device->CreateUnorderedAccessView(dst,nullptr,&y,cpu);
    cpu.ptr+=inc;D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=DXGI_FORMAT_R16G16_UNORM;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;uv.Texture2D.PlaneSlice=1;i.device->CreateUnorderedAccessView(dst,nullptr,&uv,cpu);
    ID3D12DescriptorHeap* heaps[]{i.descriptor_heap.Get()};i.command_list->SetDescriptorHeaps(1,heaps);i.command_list->SetComputeRootSignature(i.root_signature.Get());
    auto gpu=i.descriptor_heap->GetGPUDescriptorHandleForHeapStart();i.command_list->SetComputeRootDescriptorTable(0,gpu);gpu.ptr+=inc;i.command_list->SetComputeRootDescriptorTable(1,gpu);
    i.command_list->SetComputeRoot32BitConstants(2,sizeof(c)/4,&c,0);i.command_list->Dispatch((c.width+15)/16,(c.height+15)/16,1);
    barrier_count=0;
    if(!i.config.source_starts_shader_readable) {
      barriers[barrier_count].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barriers[barrier_count++].Transition={src,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON};
    }
    barriers[barrier_count].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[barrier_count++].Transition={dst,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON};
    i.command_list->ResourceBarrier(barrier_count,barriers);
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
