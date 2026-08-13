#include "digitor/windows_d3d12_yuv_converter.hpp"
#include "digitor/windows_zero_copy.hpp"

#include <atomic>
#include <cstring>
#include <new>
#include <stdexcept>
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

constexpr const char kShader[] = R"HLSL(
cbuffer C : register(b0) {
 float y_offset; float y_scale; float uv_offset; float uv_scale;
 float3 row_r; float p0; float3 row_g; float p1;
 float3 row_b; float output_scale;
 uint width; uint height; uint bit_depth; uint full_range;
};
Texture2D<float> y_plane:register(t0);
Texture2D<float2> uv_plane:register(t1);
RWTexture2D<float4> output_rgba:register(u0);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){
 if(id.x>=width||id.y>=height)return;
 float y=(y_plane.Load(int3(id.xy,0))-y_offset)*y_scale;
 float2 uv=(uv_plane.Load(int3(id.xy>>1,0))-uv_offset)*uv_scale;
 float3 v=float3(y,uv.x,uv.y);
 output_rgba[id.xy]=float4(float3(dot(row_r,v),dot(row_g,v),dot(row_b,v))*output_scale,1.0);
})HLSL";

struct Constants {
  float y_offset, y_scale, uv_offset, uv_scale;
  float row_r[3], p0, row_g[3], p1, row_b[3], output_scale;
  std::uint32_t width, height, bit_depth, full_range;
};
static_assert(sizeof(Constants) % 16 == 0);

struct FrameOwner {
  ComPtr<ID3D12Resource> input, output;
  NativeMediaSurfacePtr decoder_lifetime;
};

DigitorResult hr_result(HRESULT h) noexcept {
  return SUCCEEDED(h)
             ? DIGITOR_RESULT_OK
             : (h == E_OUTOFMEMORY ? DIGITOR_RESULT_OUT_OF_MEMORY
                                   : DIGITOR_RESULT_BACKEND_UNAVAILABLE);
}

Constants constants_for(const WindowsZeroCopySurface& s) noexcept {
  Constants o{};
  o.width = s.width;
  o.height = s.height;
  o.bit_depth = s.format == WindowsZeroCopyFormat::p010 ? 10u : 8u;
  o.full_range = s.color.full_range ? 1u : 0u;
  o.output_scale = 1.0f;

  if (s.color.full_range) {
    o.y_offset = 0.0f;
    o.y_scale = 1.0f;
    o.uv_offset = 0.5f;
    o.uv_scale = 1.0f;
  } else if (s.format == WindowsZeroCopyFormat::p010) {
    o.y_offset = 64.0f / 1023.0f;
    o.y_scale = 1023.0f / 876.0f;
    o.uv_offset = 512.0f / 1023.0f;
    o.uv_scale = 1023.0f / 896.0f;
  } else {
    o.y_offset = 16.0f / 255.0f;
    o.y_scale = 255.0f / 219.0f;
    o.uv_offset = 128.0f / 255.0f;
    o.uv_scale = 255.0f / 224.0f;
  }

  switch (s.color.matrix) {
    case WindowsYuvMatrix::bt601:
      o.row_r[0] = 1.0f;
      o.row_r[1] = 0.0f;
      o.row_r[2] = 1.4020f;
      o.row_g[0] = 1.0f;
      o.row_g[1] = -0.344136f;
      o.row_g[2] = -0.714136f;
      o.row_b[0] = 1.0f;
      o.row_b[1] = 1.7720f;
      o.row_b[2] = 0.0f;
      break;
    case WindowsYuvMatrix::bt2020_ncl:
      o.row_r[0] = 1.0f;
      o.row_r[1] = 0.0f;
      o.row_r[2] = 1.4746f;
      o.row_g[0] = 1.0f;
      o.row_g[1] = -0.164553f;
      o.row_g[2] = -0.571353f;
      o.row_b[0] = 1.0f;
      o.row_b[1] = 1.8814f;
      o.row_b[2] = 0.0f;
      break;
    case WindowsYuvMatrix::bt709:
    default:
      o.row_r[0] = 1.0f;
      o.row_r[1] = 0.0f;
      o.row_r[2] = 1.5748f;
      o.row_g[0] = 1.0f;
      o.row_g[1] = -0.187324f;
      o.row_g[2] = -0.468124f;
      o.row_b[0] = 1.0f;
      o.row_b[1] = 1.8556f;
      o.row_b[2] = 0.0f;
      break;
  }
  return o;
}
}  // namespace
#endif

struct WindowsD3D12YuvConverter::Impl {
  const void* frame_context_identity{};
  DigitorPixelFormat output_format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
  WindowsD3D12ConvertedFrameFactory frame_factory;
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
  UINT descriptor_size{};
  std::atomic_uint64_t sequence{1};
  ~Impl() {
    if (event_handle) CloseHandle(event_handle);
  }
#endif
};

WindowsD3D12YuvConverter::WindowsD3D12YuvConverter(
    void* raw, const void* frame_context_identity,
    DigitorPixelFormat output_format,
    WindowsD3D12ConvertedFrameFactory frame_factory)
    : impl_(std::make_shared<Impl>()) {
  if (!raw) throw std::invalid_argument("D3D12 device is required");
  if (output_format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT &&
      output_format != DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT)
    throw std::invalid_argument("D3D12 YUV output must be RGBA16F or RGBA32F");
  impl_->frame_context_identity =
      frame_context_identity ? frame_context_identity : raw;
  impl_->output_format = output_format;
  impl_->frame_factory = std::move(frame_factory);
#ifdef _WIN32
  impl_->device = static_cast<ID3D12Device*>(raw);
  D3D12_COMMAND_QUEUE_DESC q{};
  q.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  if (FAILED(impl_->device->CreateCommandQueue(&q, IID_PPV_ARGS(&impl_->queue))) ||
      FAILED(impl_->device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&impl_->allocator))) ||
      FAILED(impl_->device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_COMPUTE, impl_->allocator.Get(), nullptr,
          IID_PPV_ARGS(&impl_->list))))
    throw std::runtime_error("cannot create D3D12 conversion commands");
  impl_->list->Close();

  D3D12_DESCRIPTOR_HEAP_DESC hd{};
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  hd.NumDescriptors = 3;
  hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(
          impl_->device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&impl_->heap))))
    throw std::runtime_error("cannot create descriptor heap");
  impl_->descriptor_size = impl_->device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 2};
  D3D12_ROOT_PARAMETER params[2]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants = {sizeof(Constants) / 4, 0, 0};
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable = {2, ranges};
  D3D12_ROOT_SIGNATURE_DESC rs{};
  rs.NumParameters = 2;
  rs.pParameters = params;

  ComPtr<ID3DBlob> signature, errors, shader;
  if (FAILED(D3D12SerializeRootSignature(
          &rs, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)) ||
      FAILED(impl_->device->CreateRootSignature(
          0, signature->GetBufferPointer(), signature->GetBufferSize(),
          IID_PPV_ARGS(&impl_->root))) ||
      FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "yuv_to_linear_rgba.hlsl",
                        nullptr, nullptr, "main", "cs_5_1",
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors)))
    throw std::runtime_error("cannot create YUV root signature or shader");

  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
  pd.pRootSignature = impl_->root.Get();
  pd.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  if (FAILED(impl_->device->CreateComputePipelineState(
          &pd, IID_PPV_ARGS(&impl_->pipeline))) ||
      FAILED(impl_->device->CreateFence(
          0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&impl_->fence))))
    throw std::runtime_error("cannot create YUV pipeline");
  impl_->event_handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!impl_->event_handle)
    throw std::runtime_error("cannot create fence event");
#else
  (void)raw;
  throw std::runtime_error("D3D12 is unavailable");
#endif
}

WindowsD3D12YuvConverter::~WindowsD3D12YuvConverter() = default;

WindowsD3D12ConvertCallback WindowsD3D12YuvConverter::callback() {
  auto keep = impl_;
  return [keep](void* r, const WindowsZeroCopySurface& s,
                ProcessedGpuFramePtr& o) noexcept {
    WindowsD3D12YuvConverter converter(keep);
    return converter.convert(r, s, o);
  };
}

DigitorResult WindowsD3D12YuvConverter::convert(
    void* raw, const WindowsZeroCopySurface& s,
    ProcessedGpuFramePtr& out) noexcept {
  out.reset();
#ifndef _WIN32
  (void)raw;
  (void)s;
  return DIGITOR_RESULT_UNSUPPORTED;
#else
  if (!raw || !s.lifetime) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try {
    auto* input = static_cast<ID3D12Resource*>(raw);
    const auto d = input->GetDesc();
    const auto expected = s.format == WindowsZeroCopyFormat::nv12
                              ? DXGI_FORMAT_NV12
                              : DXGI_FORMAT_P010;
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        d.Format != expected || d.Width < s.width || d.Height < s.height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;

    D3D12_RESOURCE_DESC od{};
    od.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    od.Width = s.width;
    od.Height = s.height;
    od.DepthOrArraySize = 1;
    od.MipLevels = 1;
    od.Format = impl_->output_format == DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT
                    ? DXGI_FORMAT_R32G32B32A32_FLOAT
                    : DXGI_FORMAT_R16G16B16A16_FLOAT;
    od.SampleDesc.Count = 1;
    od.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> output;
    auto hr = impl_->device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &od, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr, IID_PPV_ARGS(&output));
    if (FAILED(hr)) return hr_result(hr);

    auto cpu = impl_->heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC y{};
    y.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    y.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    y.Format = s.format == WindowsZeroCopyFormat::nv12 ? DXGI_FORMAT_R8_UNORM
                                                        : DXGI_FORMAT_R16_UNORM;
    y.Texture2D.PlaneSlice = 0;
    impl_->device->CreateShaderResourceView(input, &y, cpu);
    cpu.ptr += impl_->descriptor_size;
    auto uv = y;
    uv.Format = s.format == WindowsZeroCopyFormat::nv12
                    ? DXGI_FORMAT_R8G8_UNORM
                    : DXGI_FORMAT_R16G16_UNORM;
    uv.Texture2D.PlaneSlice = 1;
    impl_->device->CreateShaderResourceView(input, &uv, cpu);
    cpu.ptr += impl_->descriptor_size;
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
    u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    u.Format = od.Format;
    impl_->device->CreateUnorderedAccessView(output.Get(), nullptr, &u, cpu);

    if (FAILED(impl_->allocator->Reset()) ||
        FAILED(impl_->list->Reset(impl_->allocator.Get(), impl_->pipeline.Get())))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    ID3D12DescriptorHeap* heaps[]{impl_->heap.Get()};
    impl_->list->SetDescriptorHeaps(1, heaps);
    impl_->list->SetComputeRootSignature(impl_->root.Get());
    const auto c = constants_for(s);
    impl_->list->SetComputeRoot32BitConstants(0, sizeof(Constants) / 4, &c, 0);
    impl_->list->SetComputeRootDescriptorTable(
        1, impl_->heap->GetGPUDescriptorHandleForHeapStart());
    impl_->list->Dispatch((s.width + 7) / 8, (s.height + 7) / 8, 1);

    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {output.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    impl_->list->ResourceBarrier(1, &b);
    if (FAILED(impl_->list->Close()))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    if (s.acquire_fence_handle && s.acquire_fence_value) {
      ComPtr<ID3D12Fence> acquire_fence;
      const auto open_fence = impl_->device->OpenSharedHandle(
          reinterpret_cast<HANDLE>(s.acquire_fence_handle),
          IID_PPV_ARGS(&acquire_fence));
      if (FAILED(open_fence) ||
          FAILED(impl_->queue->Wait(acquire_fence.Get(), s.acquire_fence_value)))
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    ID3D12CommandList* lists[]{impl_->list.Get()};
    impl_->queue->ExecuteCommandLists(1, lists);
    const auto id = impl_->sequence.fetch_add(1);
    if (FAILED(impl_->queue->Signal(impl_->fence.Get(), id)))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    if (impl_->fence->GetCompletedValue() < id) {
      if (FAILED(impl_->fence->SetEventOnCompletion(id, impl_->event_handle)))
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      WaitForSingleObject(impl_->event_handle, INFINITE);
    }

    auto owner = std::make_shared<FrameOwner>();
    owner->input = input;
    owner->output = output;
    owner->decoder_lifetime = s.lifetime;
    GpuFrameMetadata m{};
    m.width = s.width;
    m.height = s.height;
    m.format = impl_->output_format;
    m.timestamp = s.timestamp_us;
    m.color_metadata =
        (impl_->output_format == DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT
             ? "linear-rgba32f:"
             : "linear-rgba16f:") +
        std::to_string(static_cast<unsigned>(s.color.matrix)) + ":" +
        std::to_string(s.color.full_range ? 1 : 0) + ":" +
        std::to_string(s.color.primaries) + ":" +
        std::to_string(s.color.transfer);

    if (impl_->frame_factory) {
      out =
          impl_->frame_factory(input, output.Get(), s, impl_->output_format, id);
      if (!out || out->backend() != DIGITOR_RENDERER_D3D12 ||
          out->metadata().width != s.width ||
          out->metadata().height != s.height ||
          out->metadata().format != impl_->output_format ||
          out->metadata().timestamp != s.timestamp_us) {
        out.reset();
        return DIGITOR_RESULT_INTERNAL_ERROR;
      }
      return DIGITOR_RESULT_OK;
    }

    out = std::make_shared<ProcessedGpuFrame>(
        impl_->frame_context_identity, DIGITOR_RENDERER_D3D12, std::move(m), id,
        owner, std::make_shared<std::atomic_bool>(true), false);
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    out.reset();
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
#endif
}
}  // namespace digitor
