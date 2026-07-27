#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <vector>

#include "core/string_utils.hpp"
#include "core/numeric_utils.hpp"
#include "gpu/gpu_backend.hpp"
#include "gpu/native_rgb_curves.hpp"
#include "rgb_curves_shader.hpp"

namespace digitor {
namespace {
using Microsoft::WRL::ComPtr;

bool checked_uint(std::size_t value, UINT& result) noexcept {
  return checked_size_cast(value, result);
}

HRESULT compile_rgb_curves(bool texture_output, ComPtr<ID3DBlob>& bytecode,
                           ComPtr<ID3DBlob>& errors) noexcept {
  const D3D_SHADER_MACRO texture_macros[]{{"DIGITOR_TEXTURE_OUTPUT", "1"},
                                          {nullptr, nullptr}};
  return D3DCompile(digitor_rgb_curves_hlsl, sizeof(digitor_rgb_curves_hlsl),
                    "rgb_curves.hlsl", texture_output ? texture_macros : nullptr,
                    nullptr, "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3,
                    0, bytecode.ReleaseAndGetAddressOf(),
                    errors.ReleaseAndGetAddressOf());
}

struct D3DObject {
  ComPtr<ID3D12Resource> resource;
};
struct D3DPreviewOwner { ComPtr<ID3D12Resource> output,preview; D3D12_RESOURCE_STATES output_state{D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE}; };

class UniqueHandle {
public:
  UniqueHandle() = default;
  explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
  ~UniqueHandle() { reset(); }
  UniqueHandle(const UniqueHandle &) = delete;
  UniqueHandle &operator=(const UniqueHandle &) = delete;
  [[nodiscard]] HANDLE get() const noexcept { return handle_; }
  void reset(HANDLE handle = nullptr) noexcept {
    if (handle_)
      CloseHandle(handle_);
    handle_ = handle;
  }

private:
  HANDLE handle_{};
};

class MappedResource {
public:
  MappedResource() = default;
  ~MappedResource() { reset(); }
  MappedResource(const MappedResource &) = delete;
  MappedResource &operator=(const MappedResource &) = delete;

  HRESULT map(ID3D12Resource *resource,
              const D3D12_RANGE *read_range) noexcept {
    reset();
    void *data = nullptr;
    const HRESULT hr = resource->Map(0, read_range, &data);
    if (SUCCEEDED(hr)) {
      resource_ = resource;
      data_ = data;
    }
    return hr;
  }
  void reset(const D3D12_RANGE *written_range = nullptr) noexcept {
    if (resource_)
      resource_->Unmap(0, written_range);
    resource_ = nullptr;
    data_ = nullptr;
  }
  [[nodiscard]] std::uint8_t *bytes() const noexcept {
    return static_cast<std::uint8_t *>(data_);
  }

private:
  ID3D12Resource *resource_{};
  void *data_{};
};

DXGI_FORMAT format(DigitorPixelFormat value) noexcept {
  switch (value) {
  case DIGITOR_PIXEL_FORMAT_RGBA8_UNORM:
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  case DIGITOR_PIXEL_FORMAT_BGRA8_UNORM:
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT:
    return DXGI_FORMAT_R16G16B16A16_FLOAT;
  case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT:
    return DXGI_FORMAT_R32G32B32A32_FLOAT;
  default:
    return DXGI_FORMAT_UNKNOWN;
  }
}

DigitorResult result(HRESULT hr) noexcept {
  return hr == E_OUTOFMEMORY ? DIGITOR_RESULT_OUT_OF_MEMORY
                             : (FAILED(hr) ? DIGITOR_RESULT_BACKEND_UNAVAILABLE
                                           : DIGITOR_RESULT_OK);
}

void copy_tight_rgba_row(DXGI_FORMAT source_format, const std::uint8_t *source,
                         std::uint8_t *destination,
                         std::uint32_t width) noexcept {
  if (source_format == DXGI_FORMAT_R8G8B8A8_UNORM) {
    std::memcpy(destination, source, std::size_t(width) * 4);
    return;
  }
  if (source_format == DXGI_FORMAT_B8G8R8A8_UNORM) {
    for (std::uint32_t x = 0; x < width; ++x) {
      destination[x * 4] = source[x * 4 + 2];
      destination[x * 4 + 1] = source[x * 4 + 1];
      destination[x * 4 + 2] = source[x * 4];
      destination[x * 4 + 3] = source[x * 4 + 3];
    }
  }
}

class D3DBackend final : public IRenderBackend {
public:
  explicit D3DBackend(ComPtr<ID3D12Device> device)
      : device_(std::move(device)) {
    info_.backend = DIGITOR_RENDERER_D3D12;
    copy_bounded(info_.backend_name, "Direct3D 12");
    copy_bounded(info_.device_name, "D3D12 Adapter");
    info_.is_gpu = info_.supports_compute = info_.supports_fp16 =
        info_.supports_fp32 = 1;
  }
  ~D3DBackend() override { shutdown(); }

  bool initialize(bool) override {
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    if (FAILED(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_))))
      return false;
    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&allocator_))))
      return false;
    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          allocator_.Get(), nullptr,
                                          IID_PPV_ARGS(&list_))))
      return false;
    if (FAILED(list_->Close()))
      return false;
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                    IID_PPV_ARGS(&fence_))))
      return false;
    fence_event_.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    if (!fence_event_.get())
      return false;

    D3D12_ROOT_SIGNATURE_DESC root_desc{};
    root_desc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    if (FAILED(D3D12SerializeRootSignature(
            &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)))
      return false;
    if (FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            IID_PPV_ARGS(&root_signature_))))
      return false;

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = 1;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&heap_desc,
                                             IID_PPV_ARGS(&srv_heap_))))
      return false;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    return SUCCEEDED(
        device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap_)));
  }

  void shutdown() noexcept override {
    if (queue_ && fence_)
      (void)signal_and_wait();
    rtv_heap_.Reset();
    srv_heap_.Reset();
    root_signature_.Reset();
    fence_.Reset();
    list_.Reset();
    allocator_.Reset();
    queue_.Reset();
    fence_event_.reset();
  }
  [[nodiscard]] DigitorRendererInfo info() const noexcept override {
    return info_;
  }

  DigitorResult
  render_rgba8(uint32_t width, uint32_t height, std::span<const uint8_t> source,
               std::vector<uint8_t> &destination) noexcept override {
    constexpr DXGI_FORMAT texture_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    const std::size_t pixel_bytes = std::size_t(width) * height * 4;
    if (!width || !height || (!source.empty() && source.size() != pixel_bytes))
      return DIGITOR_RESULT_INVALID_ARGUMENT;

    D3D12_RESOURCE_DESC texture_desc{};
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = width;
    texture_desc.Height = height;
    texture_desc.DepthOrArraySize = 1;
    texture_desc.MipLevels = 1;
    texture_desc.Format = texture_format;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const auto initial_state = source.empty()
                                   ? D3D12_RESOURCE_STATE_RENDER_TARGET
                                   : D3D12_RESOURCE_STATE_COPY_DEST;
    ComPtr<ID3D12Resource> target;
    HRESULT hr = device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &texture_desc, initial_state, nullptr,
        IID_PPV_ARGS(&target));
    if (FAILED(hr))
      return result(hr);

    UINT64 total_bytes = 0;
    UINT64 unpadded_row_bytes = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    device_->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, nullptr,
                                   &unpadded_row_bytes, &total_bytes);
    const UINT row_pitch = footprint.Footprint.RowPitch;
    if (unpadded_row_bytes != UINT64(width) * 4 ||
        row_pitch < unpadded_row_bytes)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = total_bytes;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> upload;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    hr = device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
    if (FAILED(hr))
      return result(hr);
    ComPtr<ID3D12Resource> readback;
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    hr = device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));
    if (FAILED(hr))
      return result(hr);

    MappedResource upload_map;
    D3D12_RANGE no_read{0, 0};
    hr = upload_map.map(upload.Get(), &no_read);
    if (FAILED(hr))
      return result(hr);
    for (UINT y = 0; y < height; ++y) {
      auto *row = upload_map.bytes() + std::size_t(y) * row_pitch;
      if (!source.empty())
        std::memcpy(row, source.data() + std::size_t(y) * width * 4,
                    std::size_t(width) * 4);
      else
        for (UINT x = 0; x < width; ++x) {
          row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 0;
          row[x * 4 + 3] = 255;
        }
    }
    upload_map.reset();

    hr = allocator_->Reset();
    if (FAILED(hr))
      return result(hr);
    hr = list_->Reset(allocator_.Get(), nullptr);
    if (FAILED(hr))
      return result(hr);
    D3D12_TEXTURE_COPY_LOCATION target_location{};
    target_location.pResource = target.Get();
    target_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    if (source.empty()) {
      const auto rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
      device_->CreateRenderTargetView(target.Get(), nullptr, rtv);
      constexpr float clear_color[4]{0.0f, 0.0f, 0.0f, 1.0f};
      list_->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
    } else {
      D3D12_TEXTURE_COPY_LOCATION upload_location{};
      upload_location.pResource = upload.Get();
      upload_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      upload_location.PlacedFootprint = footprint;
      list_->CopyTextureRegion(&target_location, 0, 0, 0, &upload_location,
                               nullptr);
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target.Get();
    barrier.Transition.Subresource = 0;
    barrier.Transition.StateBefore = source.empty()
                                         ? D3D12_RESOURCE_STATE_RENDER_TARGET
                                         : D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list_->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION readback_location{};
    readback_location.pResource = readback.Get();
    readback_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    readback_location.PlacedFootprint = footprint;
    list_->CopyTextureRegion(&readback_location, 0, 0, 0, &target_location,
                             nullptr);
    hr = list_->Close();
    if (FAILED(hr))
      return result(hr);
    ID3D12CommandList *command_lists[]{list_.Get()};
    queue_->ExecuteCommandLists(1, command_lists);
    const auto wait_result = signal_and_wait();
    if (wait_result != DIGITOR_RESULT_OK)
      return wait_result;

    try {
      destination.resize(pixel_bytes);
    } catch (const std::bad_alloc &) {
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    MappedResource readback_map;
    D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_bytes)};
    hr = readback_map.map(readback.Get(), &read_range);
    if (FAILED(hr))
      return result(hr);
    for (UINT y = 0; y < height; ++y)
      copy_tight_rgba_row(
          texture_format, readback_map.bytes() + std::size_t(y) * row_pitch,
          destination.data() + std::size_t(y) * width * 4, width);
    D3D12_RANGE no_write{0, 0};
    readback_map.reset(&no_write);
    return DIGITOR_RESULT_OK;
  }

  DigitorResult grade_rgba32f(std::span<const Color> source,
                              std::span<Color> out,
                              const ColorGrade &p) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_D3D12, true, info_.device_name,
                           "D3DCompile cs_5_1", "grade-hlsl-v1:main",
                           "ID3D12PipelineState:grade-v1");
    if (gpu_failure_point() != GpuFailurePoint::None)
      return injected_failure(gpu_failure_point());
    if (source.size() != out.size())
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (source.empty())
      return DIGITOR_RESULT_OK;
    UINT source_count = 0;
    if (!checked_uint(source.size(), source_count))
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    static constexpr char shader[] =
        R"(struct P{float exposure,contrast,gamma_,lift,gain,offset_,temperature,tint,saturation,vibrance,hue;uint count;}; StructuredBuffer<float4> input:register(t0); RWStructuredBuffer<float4> output:register(u0); ConstantBuffer<P> p:register(b0); [numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){uint k=id.x;if(k>=p.count)return;float4 c=input[k];float3 x=c.rgb;float t=p.temperature*.1;x.r+=t;x.b-=t;x.g+=p.tint*.1;float l=dot(x,float3(.2126,.7152,.0722));float v=1+p.vibrance*(1-(max(x.r,max(x.g,x.b))-min(x.r,min(x.g,x.b))));x=l+(x-l)*(p.saturation*v);x=(x-.5)*p.contrast+.5;x=(x+p.lift)*p.gain+p.offset_;x*=exp2(p.exposure);x=sign(x)*pow(abs(x),1/max(.001,p.gamma_));float a=p.hue*.0174532925199433,co=cos(a),s=sin(a);float3 r=x;x=float3((.213+co*.787-s*.213)*r.r+(.715-co*.715-s*.715)*r.g+(.072-co*.072+s*.928)*r.b,(.213-co*.213+s*.143)*r.r+(.715+co*.285+s*.140)*r.g+(.072-co*.072-s*.283)*r.b,(.213-co*.213-s*.787)*r.r+(.715-co*.715+s*.715)*r.g+(.072+co*.928+s*.072)*r.b);output[k]=float4(x,c.a);})";
    ComPtr<ID3DBlob> bytecode, errors;
    if (FAILED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr,
                          "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                          &bytecode, &errors)))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0};
    ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0};
    D3D12_ROOT_PARAMETER roots[3]{};
    for (int k = 0; k < 2; k++) {
      roots[k].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      roots[k].DescriptorTable = {1, &ranges[k]};
    }
    roots[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    roots[2].Constants = {0, 0, 12};
    D3D12_ROOT_SIGNATURE_DESC rd{3, roots, 0, nullptr,
                                 D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> sig;
    if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1,
                                           &sig, &errors)))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    ComPtr<ID3D12RootSignature> root;
    if (FAILED(device_->CreateRootSignature(0, sig->GetBufferPointer(),
                                            sig->GetBufferSize(),
                                            IID_PPV_ARGS(&root))))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root.Get();
    pd.CS = {bytecode->GetBufferPointer(), bytecode->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(
            device_->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipeline))))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    const UINT64 bytes = source.size_bytes();
    auto make_buffer = [&](D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state,
                           D3D12_RESOURCE_FLAGS flags,
                           ComPtr<ID3D12Resource> &resource) {
      D3D12_HEAP_PROPERTIES hp{};
      hp.Type = type;
      D3D12_RESOURCE_DESC bd{};
      bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      bd.Width = bytes;
      bd.Height = 1;
      bd.DepthOrArraySize = 1;
      bd.MipLevels = 1;
      bd.SampleDesc.Count = 1;
      bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      bd.Flags = flags;
      return device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
                                              state, nullptr,
                                              IID_PPV_ARGS(&resource));
    };
    ComPtr<ID3D12Resource> input, output, readback;
    if (FAILED(make_buffer(D3D12_HEAP_TYPE_UPLOAD,
                           D3D12_RESOURCE_STATE_GENERIC_READ,
                           D3D12_RESOURCE_FLAG_NONE, input)) ||
        FAILED(make_buffer(
            D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, output)) ||
        FAILED(make_buffer(D3D12_HEAP_TYPE_READBACK,
                           D3D12_RESOURCE_STATE_COPY_DEST,
                           D3D12_RESOURCE_FLAG_NONE, readback)))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    void *m = nullptr;
    D3D12_RANGE none{0, 0};
    input->Map(0, &none, &m);
    std::memcpy(m, source.data(), bytes);
    input->Unmap(0, nullptr);
    provenance_.source_upload_performed = true;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2,
                                  D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    ComPtr<ID3D12DescriptorHeap> heap;
    if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap))))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Buffer.NumElements = source_count;
    static_assert(sizeof(Color) <= UINT_MAX);
    sd.Buffer.StructureByteStride = static_cast<UINT>(sizeof(Color));
    device_->CreateShaderResourceView(input.Get(), &sd, cpu);
    cpu.ptr += stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = source_count;
    ud.Buffer.StructureByteStride = static_cast<UINT>(sizeof(Color));
    device_->CreateUnorderedAccessView(output.Get(), nullptr, &ud, cpu);
    allocator_->Reset();
    list_->Reset(allocator_.Get(), pipeline.Get());
    ID3D12DescriptorHeap *heaps[]{heap.Get()};
    list_->SetDescriptorHeaps(1, heaps);
    list_->SetComputeRootSignature(root.Get());
    auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
    list_->SetComputeRootDescriptorTable(0, gpu);
    gpu.ptr += stride;
    list_->SetComputeRootDescriptorTable(1, gpu);
    struct Push {
      float v[11];
      uint32_t n;
    } push{{p.exposure, p.contrast, p.gamma, p.lift, p.gain, p.offset,
            p.temperature, p.tint, p.saturation, p.vibrance, p.hue},
           source_count};
    list_->SetComputeRoot32BitConstants(2, 12, &push, 0);
    list_->Dispatch((push.n + 63) / 64, 1, 1);
    provenance_.command_recorded = true;
    provenance_.dispatch_or_draw_issued = true;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {output.Get(), 0,
                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          D3D12_RESOURCE_STATE_COPY_SOURCE};
    list_->ResourceBarrier(1, &barrier);
    list_->CopyResource(readback.Get(), output.Get());
    list_->Close();
    ID3D12CommandList *lists[]{list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    provenance_.queue_submission_issued = true;
    auto result = signal_and_wait();
    if (result != DIGITOR_RESULT_OK)
      return result;
    provenance_.synchronization_waited = true;
    D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
    readback->Map(0, &range, &m);
    std::memcpy(out.data(), m, bytes);
    readback->Unmap(0, &none);
    provenance_.output_written = true;
    provenance_.readback_performed = true;
    provenance_.cpu_color_reference_invocations =
      cpu_color_reference_count() - provenance_.cpu_color_reference_invocations;
    return DIGITOR_RESULT_OK;
  }

  DigitorResult execute_process_curves_gpu(std::span<const Color> source,uint32_t width,uint32_t height,int64_t timestamp,const CompiledRgbCurves&curves,ProcessedGpuFramePtr&out)noexcept override{
    out.reset();begin_grade_provenance(DIGITOR_RENDERER_D3D12,true,info_.device_name,"D3DCompile cs_5_1","rgb_curves.hlsl:texture","D3D12 texture curves");if(!width||!height||source.size()!=size_t(width)*height)return DIGITOR_RESULT_INVALID_ARGUMENT;
    ComPtr<ID3DBlob> shader,error;if(FAILED(compile_rgb_curves(true,shader,error))){provenance_.failure_stage="RGB Curves texture shader compilation";return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0};D3D12_ROOT_PARAMETER roots[3]{};roots[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;roots[0].DescriptorTable={1,&ranges[0]};roots[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;roots[1].DescriptorTable={1,&ranges[1]};roots[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;constexpr UINT dwords=sizeof(NativeRgbCurvesParameters)/4;roots[2].Constants={0,0,dwords};D3D12_ROOT_SIGNATURE_DESC rd{3,roots,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob>sig,root_error;if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&root_error)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;ComPtr<ID3D12RootSignature>root;if(FAILED(device_->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&root))))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={shader->GetBufferPointer(),shader->GetBufferSize()};ComPtr<ID3D12PipelineState>pipeline;if(FAILED(device_->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipeline))))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto committed=[&](const D3D12_RESOURCE_DESC&d,D3D12_HEAP_TYPE ht,D3D12_RESOURCE_STATES st,ComPtr<ID3D12Resource>&r){D3D12_HEAP_PROPERTIES hp{};hp.Type=ht;return device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,st,nullptr,IID_PPV_ARGS(&r));};D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=width;td.Height=height;td.DepthOrArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;ComPtr<ID3D12Resource>input;auto owner=std::make_shared<D3DPreviewOwner>();if(FAILED(committed(td,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,input)))return DIGITOR_RESULT_OUT_OF_MEMORY;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;if(FAILED(committed(td,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,owner->output)))return DIGITOR_RESULT_OUT_OF_MEMORY;td.Flags=D3D12_RESOURCE_FLAG_NONE;if(FAILED(committed(td,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,owner->preview)))return DIGITOR_RESULT_OUT_OF_MEMORY;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 row_bytes=0,total=0;device_->GetCopyableFootprints(&td,0,1,0,&footprint,&rows,&row_bytes,&total);D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource>upload;if(FAILED(committed(bd,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,upload)))return DIGITOR_RESULT_OUT_OF_MEMORY;void*m=nullptr;D3D12_RANGE none{0,0};if(FAILED(upload->Map(0,&none,&m)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;for(uint32_t y=0;y<height;y++)std::memcpy(static_cast<std::byte*>(m)+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch,source.data()+size_t(y)*width,size_t(width)*sizeof(Color));upload->Unmap(0,nullptr);
    auto lut=native_rgb_curves_lut(curves);UINT lut_count=0;if(!checked_uint(lut.size(),lut_count))return DIGITOR_RESULT_INVALID_ARGUMENT;bd.Width=lut.size()*sizeof(float);ComPtr<ID3D12Resource>lb;if(FAILED(committed(bd,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,lb)))return DIGITOR_RESULT_OUT_OF_MEMORY;if(FAILED(lb->Map(0,&none,&m)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;std::memcpy(m,lut.data(),lut.size()*sizeof(float));lb->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};ComPtr<ID3D12DescriptorHeap>heap;if(FAILED(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;auto cpu=heap->GetCPUDescriptorHandleForHeapStart();auto stride=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;device_->CreateShaderResourceView(input.Get(),&sd,cpu);cpu.ptr+=stride;sd={};sd.Format=DXGI_FORMAT_UNKNOWN;sd.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Buffer.NumElements=lut_count;sd.Buffer.StructureByteStride=sizeof(float);device_->CreateShaderResourceView(lb.Get(),&sd,cpu);cpu.ptr+=stride;D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;ud.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device_->CreateUnorderedAccessView(owner->output.Get(),nullptr,&ud,cpu);
    allocator_->Reset();list_->Reset(allocator_.Get(),pipeline.Get());D3D12_TEXTURE_COPY_LOCATION dst{input.Get(),D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};D3D12_TEXTURE_COPY_LOCATION src{upload.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};src.PlacedFootprint=footprint;list_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);D3D12_RESOURCE_BARRIER rb{};rb.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;rb.Transition={input.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};list_->ResourceBarrier(1,&rb);ID3D12DescriptorHeap*heaps[]{heap.Get()};list_->SetDescriptorHeaps(1,heaps);list_->SetComputeRootSignature(root.Get());auto gpu=heap->GetGPUDescriptorHandleForHeapStart();list_->SetComputeRootDescriptorTable(0,gpu);gpu.ptr+=2*stride;list_->SetComputeRootDescriptorTable(1,gpu);auto params=native_rgb_curves_parameters(curves,uint32_t(source.size()));params.padding[0]=width;params.padding[1]=height;list_->SetComputeRoot32BitConstants(2,dwords,&params,0);list_->Dispatch((uint32_t(source.size())+63)/64,1,1);rb.Transition={owner->output.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};list_->ResourceBarrier(1,&rb);if(FAILED(list_->Close()))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;ID3D12CommandList*lists[]{list_.Get()};queue_->ExecuteCommandLists(1,lists);auto status=signal_and_wait();if(status!=DIGITOR_RESULT_OK)return status;
    static std::atomic_uint64_t ids{1};out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_D3D12,GpuFrameMetadata{width,height,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuFrameAlpha::straight,timestamp,"linear-rgba"},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),true);provenance_.curve_source_bound=provenance_.curve_destination_bound=provenance_.curve_lut_bound=provenance_.curve_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=true;provenance_.readback_performed=false;return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_present_gpu_frame(const ProcessedGpuFramePtr&frame)noexcept override{if(!frame||frame->acquire(this,DIGITOR_RENDERER_D3D12)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;auto o=std::static_pointer_cast<D3DPreviewOwner>(native_owner(*frame));if(!o){(void)frame->release(this);return DIGITOR_RESULT_INVALID_ARGUMENT;}allocator_->Reset();list_->Reset(allocator_.Get(),nullptr);D3D12_RESOURCE_BARRIER b[2]{};for(auto&x:b)x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b[0].Transition={o->output.Get(),0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};b[1].Transition={o->preview.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_DEST};list_->ResourceBarrier(1,b);list_->CopyResource(o->preview.Get(),o->output.Get());std::swap(b[0].Transition.StateBefore,b[0].Transition.StateAfter);b[1].Transition={o->preview.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};list_->ResourceBarrier(2,b);auto result=list_->Close();if(SUCCEEDED(result)){ID3D12CommandList*l[]{list_.Get()};queue_->ExecuteCommandLists(1,l);result=signal_and_wait()==DIGITOR_RESULT_OK?S_OK:E_FAIL;}(void)frame->release(this);return SUCCEEDED(result)?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;}

  DigitorResult execute_curves_rgba32f(std::span<const Color> source,std::span<Color> out,
                               const CompiledRgbCurves& curves) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_D3D12,true,info_.device_name,
      "D3DCompile cs_5_1","rgb_curves.hlsl:main","ID3D12PipelineState:rgb-curves-v1");
    provenance_.curves_enabled=true;provenance_.curve_lut_size=curves.lut_size();provenance_.compiled_curve_identity=curves.identity();provenance_.native_curve_shader_identity="rgb_curves.hlsl:DXIL-abi-v1";provenance_.native_lut_resource_identity=curves.identity()+":"+info_.device_name;
    if(gpu_failure_point()!=GpuFailurePoint::None)return injected_failure(gpu_failure_point());if(source.size()!=out.size())return DIGITOR_RESULT_INVALID_ARGUMENT;if(source.empty())return DIGITOR_RESULT_OK;UINT source_count=0;if(!checked_uint(source.size(),source_count))return DIGITOR_RESULT_INVALID_ARGUMENT;
    ComPtr<ID3DBlob> shader,error;if(FAILED(compile_rgb_curves(false,shader,error))){provenance_.failure_stage="RGB Curves validation shader compilation";return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}provenance_.shader_pipeline_cache=CacheDisposition::NotApplicable;
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0};D3D12_ROOT_PARAMETER roots[3]{};roots[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;roots[0].DescriptorTable={1,&ranges[0]};roots[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;roots[1].DescriptorTable={1,&ranges[1]};roots[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;static_assert(sizeof(NativeRgbCurvesParameters)/4<=UINT_MAX);constexpr UINT parameter_dwords=static_cast<UINT>(sizeof(NativeRgbCurvesParameters)/4);roots[2].Constants={0,0,parameter_dwords};D3D12_ROOT_SIGNATURE_DESC rd{3,roots,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> sig,root_error;if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&sig,&root_error)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;ComPtr<ID3D12RootSignature> root;if(FAILED(device_->CreateRootSignature(0,sig->GetBufferPointer(),sig->GetBufferSize(),IID_PPV_ARGS(&root))))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={shader->GetBufferPointer(),shader->GetBufferSize()};ComPtr<ID3D12PipelineState> pipeline;if(FAILED(device_->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipeline))))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    const auto lut=native_rgb_curves_lut(curves);UINT lut_count=0;if(!checked_uint(lut.size(),lut_count))return DIGITOR_RESULT_INVALID_ARGUMENT;const auto params=native_rgb_curves_parameters(curves,source_count);const UINT64 pixel_bytes=static_cast<UINT64>(source.size_bytes()),lut_bytes=static_cast<UINT64>(lut.size()*sizeof(float));auto make=[&](UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags,ComPtr<ID3D12Resource>&r){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;return device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r));};ComPtr<ID3D12Resource>input,lut_resource,output,readback;if(FAILED(make(pixel_bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE,input))||FAILED(make(lut_bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_FLAG_NONE,lut_resource))||FAILED(make(pixel_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,output))||FAILED(make(pixel_bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_NONE,readback)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;void*m=nullptr;D3D12_RANGE none{0,0};input->Map(0,&none,&m);std::memcpy(m,source.data(),pixel_bytes);input->Unmap(0,nullptr);lut_resource->Map(0,&none,&m);std::memcpy(m,lut.data(),lut_bytes);lut_resource->Unmap(0,nullptr);provenance_.source_upload_performed=true;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};ComPtr<ID3D12DescriptorHeap>heap;if(FAILED(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;auto cpu=heap->GetCPUDescriptorHandleForHeapStart();UINT stride=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_UNKNOWN;sd.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Buffer.NumElements=source_count;static_assert(sizeof(Color)<=UINT_MAX);sd.Buffer.StructureByteStride=static_cast<UINT>(sizeof(Color));device_->CreateShaderResourceView(input.Get(),&sd,cpu);cpu.ptr+=stride;sd.Buffer.NumElements=lut_count;static_assert(sizeof(float)<=UINT_MAX);sd.Buffer.StructureByteStride=static_cast<UINT>(sizeof(float));device_->CreateShaderResourceView(lut_resource.Get(),&sd,cpu);cpu.ptr+=stride;D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_UNKNOWN;ud.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=source_count;ud.Buffer.StructureByteStride=static_cast<UINT>(sizeof(Color));device_->CreateUnorderedAccessView(output.Get(),nullptr,&ud,cpu);
    allocator_->Reset();list_->Reset(allocator_.Get(),pipeline.Get());ID3D12DescriptorHeap*heaps[]{heap.Get()};list_->SetDescriptorHeaps(1,heaps);list_->SetComputeRootSignature(root.Get());auto gpu=heap->GetGPUDescriptorHandleForHeapStart();list_->SetComputeRootDescriptorTable(0,gpu);gpu.ptr+=2*stride;list_->SetComputeRootDescriptorTable(1,gpu);list_->SetComputeRoot32BitConstants(2,parameter_dwords,&params,0);list_->Dispatch((params.pixel_count+63)/64,1,1);provenance_.command_recorded=provenance_.dispatch_or_draw_issued=true;D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={output.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};list_->ResourceBarrier(1,&barrier);list_->CopyResource(readback.Get(),output.Get());if(FAILED(list_->Close()))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;ID3D12CommandList*lists[]{list_.Get()};queue_->ExecuteCommandLists(1,lists);provenance_.queue_submission_issued=true;auto status=signal_and_wait();if(status!=DIGITOR_RESULT_OK)return status;provenance_.synchronization_waited=true;D3D12_RANGE read{0,static_cast<SIZE_T>(pixel_bytes)};if(FAILED(readback->Map(0,&read,&m)))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;std::memcpy(out.data(),m,pixel_bytes);readback->Unmap(0,&none);provenance_.curve_source_bound=provenance_.curve_destination_bound=provenance_.curve_lut_bound=provenance_.curve_parameters_bound=true;provenance_.native_lut_cache=CacheDisposition::Miss;provenance_.output_written=provenance_.readback_performed=provenance_.validation_readback_completed=true;return DIGITOR_RESULT_OK;
  }

  DigitorResult create_texture(const DigitorTextureDesc &desc,
                               void **out) noexcept override {
    if (!out)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out = nullptr;
    auto object = std::unique_ptr<D3DObject>(new (std::nothrow) D3DObject);
    if (!object)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource.Width = desc.width;
    resource.Height = desc.height;
    resource.DepthOrArraySize = 1;
    resource.MipLevels = 1;
    resource.Format = format(desc.format);
    resource.SampleDesc.Count = 1;
    resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (desc.usage & DIGITOR_TEXTURE_USAGE_RENDER_TARGET)
      resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (desc.usage & DIGITOR_TEXTURE_USAGE_STORAGE)
      resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const HRESULT hr = device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &resource, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&object->resource));
    if (FAILED(hr))
      return result(hr);
    *out = object.release();
    return DIGITOR_RESULT_OK;
  }

  DigitorResult create_buffer(const DigitorBufferDesc &desc,
                              void **out) noexcept override {
    if (!out)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out = nullptr;
    auto object = std::unique_ptr<D3DObject>(new (std::nothrow) D3DObject);
    if (!object)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = (desc.usage &
                 (DIGITOR_BUFFER_USAGE_UPLOAD | DIGITOR_BUFFER_USAGE_STAGING))
                    ? D3D12_HEAP_TYPE_UPLOAD
                    : D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource.Width = desc.size;
    resource.Height = 1;
    resource.DepthOrArraySize = 1;
    resource.MipLevels = 1;
    resource.SampleDesc.Count = 1;
    resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (desc.usage & DIGITOR_BUFFER_USAGE_STORAGE)
      resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto state = heap.Type == D3D12_HEAP_TYPE_UPLOAD
                           ? D3D12_RESOURCE_STATE_GENERIC_READ
                           : D3D12_RESOURCE_STATE_COMMON;
    const HRESULT hr = device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &resource, state, nullptr,
        IID_PPV_ARGS(&object->resource));
    if (FAILED(hr))
      return result(hr);
    *out = object.release();
    return DIGITOR_RESULT_OK;
  }

  DigitorResult create_sampler(const DigitorSamplerDesc &desc,
                               void **out) noexcept override {
    if (!out)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto *sampler = new (std::nothrow) DigitorSamplerDesc(desc);
    *out = sampler;
    return sampler ? DIGITOR_RESULT_OK : DIGITOR_RESULT_OUT_OF_MEMORY;
  }
  DigitorResult map_buffer(void *pointer, uint64_t offset, uint64_t,
                           void **out) noexcept override {
    if (!pointer || !out)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    *out = nullptr;
    auto *object = static_cast<D3DObject *>(pointer);
    D3D12_RANGE no_read{0, 0};
    void *base = nullptr;
    const HRESULT hr = object->resource->Map(0, &no_read, &base);
    if (FAILED(hr))
      return result(hr);
    *out = static_cast<unsigned char *>(base) + offset;
    return DIGITOR_RESULT_OK;
  }
  void unmap_buffer(void *pointer) noexcept override {
    if (pointer)
      static_cast<D3DObject *>(pointer)->resource->Unmap(0, nullptr);
  }
  void destroy_texture(void *pointer) noexcept override {
    delete static_cast<D3DObject *>(pointer);
  }
  void destroy_buffer(void *pointer) noexcept override {
    delete static_cast<D3DObject *>(pointer);
  }
  void destroy_sampler(void *pointer) noexcept override {
    delete static_cast<DigitorSamplerDesc *>(pointer);
  }

private:
  DigitorResult signal_and_wait() noexcept {
    const UINT64 value = ++fence_value_;
    HRESULT hr = queue_->Signal(fence_.Get(), value);
    if (FAILED(hr))
      return result(hr);
    if (fence_->GetCompletedValue() >= value)
      return DIGITOR_RESULT_OK;
    hr = fence_->SetEventOnCompletion(value, fence_event_.get());
    if (FAILED(hr))
      return result(hr);
    return WaitForSingleObject(fence_event_.get(), INFINITE) == WAIT_OBJECT_0
               ? DIGITOR_RESULT_OK
               : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<ID3D12CommandAllocator> allocator_;
  ComPtr<ID3D12GraphicsCommandList> list_;
  ComPtr<ID3D12Fence> fence_;
  UniqueHandle fence_event_;
  UINT64 fence_value_{};
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12DescriptorHeap> srv_heap_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  DigitorRendererInfo info_{};
};
} // namespace

std::unique_ptr<IRenderBackend>
create_native_backend(DigitorRendererBackend backend) {
#ifdef DIGITOR_HAS_VULKAN
  extern std::unique_ptr<IRenderBackend> create_vulkan_backend();
  if (backend == DIGITOR_RENDERER_VULKAN)
    return create_vulkan_backend();
#endif
  if (backend != DIGITOR_RENDERER_D3D12)
    return nullptr;
  if (gpu_validation_requested()) {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
      debug->EnableDebugLayer();
  }
  ComPtr<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&device))))
    return nullptr;
  return std::make_unique<D3DBackend>(std::move(device));
}
} // namespace digitor
#endif
