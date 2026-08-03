#include "digitor/windows_d3d12_builtin_effect_shaders.hpp"

#if defined(_WIN32)

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

namespace digitor {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char kEffectShader[] = R"HLSL(
Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Target : register(u0);
cbuffer EffectConstants : register(b0) {
  uint EffectKind;
  uint Width;
  uint Height;
  uint PassIndex;
  float Amount;
  float Radius;
  float Angle;
  uint SeedLo;
  uint SeedHi;
  uint Quality;
  uint Reserved0;
  uint Reserved1;
};

int2 ClampCoord(int2 p) {
  return clamp(p, int2(0, 0), int2(int(Width) - 1, int(Height) - 1));
}
float4 LoadAt(int2 p) { return Source.Load(int3(ClampCoord(p), 0)); }
uint Hash(uint x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
  x *= 0x846ca68bu; x ^= x >> 16; return x;
}
float Noise01(uint2 p) {
  uint v = Hash(p.x + p.y * Width + SeedLo + Hash(SeedHi));
  return (v & 0x00ffffffu) / 16777216.0;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  if (tid.x >= Width || tid.y >= Height) return;
  int2 p = int2(tid.xy);
  float4 base = LoadAt(p);
  float4 result = base;
  int r = clamp(int(ceil(abs(Radius))), 1, 64);

  if (EffectKind == 0u || EffectKind == 2u) {
    float4 sum = 0.0; float weightSum = 0.0;
    float sigma = max(1.0, Radius * 0.45);
    int2 axis = PassIndex == 0u ? int2(1, 0) : int2(0, 1);
    for (int i = -r; i <= r; ++i) {
      float w = exp(-0.5 * (i * i) / (sigma * sigma));
      float4 sampleValue = LoadAt(p + axis * i);
      if (EffectKind == 2u && PassIndex == 0u) {
        float lum = max(sampleValue.r, max(sampleValue.g, sampleValue.b));
        sampleValue.rgb *= saturate((lum - 0.5) * 2.0);
      }
      sum += sampleValue * w; weightSum += w;
    }
    result = sum / max(weightSum, 0.00001);
    result.a = base.a;
  } else if (EffectKind == 1u) {
    float4 blur = (LoadAt(p + int2(-1,0)) + LoadAt(p + int2(1,0)) +
                   LoadAt(p + int2(0,-1)) + LoadAt(p + int2(0,1))) * 0.25;
    result.rgb = base.rgb + (base.rgb - blur.rgb) * Amount;
  } else if (EffectKind == 3u) {
    float2 uv = (float2(tid.xy) + 0.5) / float2(Width, Height) * 2.0 - 1.0;
    float k = 1.0 + Amount * dot(uv, uv);
    float2 sourceUv = uv * k;
    int2 source = int2((sourceUv * 0.5 + 0.5) * float2(Width, Height));
    result = LoadAt(source);
    result.a = base.a;
  } else if (EffectKind == 4u || EffectKind == 5u) {
    float n = (Noise01(tid.xy) - 0.5) * Amount;
    if (EffectKind == 5u) n *= 0.75 + 0.25 * sin((tid.x + tid.y) * 0.41);
    result.rgb = base.rgb + n;
  } else if (EffectKind == 6u) {
    int offset = max(1, r);
    float4 left = LoadAt(p + int2(-offset, 0));
    float4 right = LoadAt(p + int2(offset, 0));
    result = float4(left.r, base.g, right.b, base.a);
  } else if (EffectKind == 7u) {
    float2 uv = (float2(tid.xy) + 0.5) / float2(Width, Height) * 2.0 - 1.0;
    float factor = 1.0 - saturate(dot(uv, uv) * 0.5 * Amount);
    result.rgb = base.rgb * factor;
  } else if (EffectKind == 8u) {
    float2 direction = float2(cos(Angle), sin(Angle));
    if (PassIndex == 1u) direction = float2(-direction.y, direction.x) * 0.35;
    float4 sum = 0.0; uint count = 0u;
    for (int i = -r; i <= r; ++i) {
      sum += LoadAt(p + int2(round(direction * i))); ++count;
    }
    result = sum / max(1u, count); result.a = base.a;
  }
  result.a = base.a;
  Target[tid.xy] = result;
}
)HLSL";

struct ShaderConstants {
  std::uint32_t kind{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t pass_index{};
  float amount{};
  float radius{};
  float angle{};
  std::uint32_t seed_lo{};
  std::uint32_t seed_hi{};
  std::uint32_t quality{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
};
static_assert(sizeof(ShaderConstants) == 48);

bool effect_kind(EffectType type, std::uint32_t& value) noexcept {
  switch (type) {
    case EffectType::blur: value = 0; return true;
    case EffectType::sharpen: value = 1; return true;
    case EffectType::glow: value = 2; return true;
    case EffectType::lens_distortion: value = 3; return true;
    case EffectType::noise: value = 4; return true;
    case EffectType::film_grain: value = 5; return true;
    case EffectType::chromatic_aberration: value = 6; return true;
    case EffectType::vignette: value = 7; return true;
    case EffectType::motion_blur: value = 8; return true;
  }
  return false;
}

DXGI_FORMAT resource_format(ID3D12Resource* resource) noexcept {
  return resource ? resource->GetDesc().Format : DXGI_FORMAT_UNKNOWN;
}

struct BuiltinShaderState final {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12PipelineState> pipeline;
  ComPtr<ID3D12DescriptorHeap> heap;
  UINT descriptor_size{};
  UINT cursor{};
  std::mutex mutex;
};

}  // namespace

WindowsD3D12BuiltinEffectShadersResult
create_windows_d3d12_builtin_effect_shaders(void* native_device) noexcept {
  WindowsD3D12BuiltinEffectShadersResult out{};
  if (!native_device) {
    out.diagnostic = "D3D12 built-in shader package requires a device";
    return out;
  }

  auto state = std::make_shared<BuiltinShaderState>();
  state->device = static_cast<ID3D12Device*>(native_device);

  ComPtr<ID3DBlob> shader;
  ComPtr<ID3DBlob> errors;
  const HRESULT compile_hr = D3DCompile(
      kEffectShader, sizeof(kEffectShader) - 1, "digitor_builtin_effects.hlsl",
      nullptr, nullptr, "main", "cs_5_1",
      D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
      &shader, &errors);
  if (FAILED(compile_hr) || !shader) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = errors
        ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
        : "D3D12 built-in effect shader compilation failed";
    return out;
  }

  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 1;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].OffsetInDescriptorsFromTableStart = 1;

  D3D12_ROOT_PARAMETER parameters[2]{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 2;
  parameters[0].DescriptorTable.pDescriptorRanges = ranges;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = sizeof(ShaderConstants) / 4;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC root_desc{};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  ComPtr<ID3DBlob> serialized;
  errors.Reset();
  if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &serialized, &errors)) ||
      FAILED(state->device->CreateRootSignature(
          0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
          IID_PPV_ARGS(&state->root_signature)))) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "D3D12 built-in effect root signature creation failed";
    return out;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = state->root_signature.Get();
  pso_desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
  if (FAILED(state->device->CreateComputePipelineState(
          &pso_desc, IID_PPV_ARGS(&state->pipeline)))) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "D3D12 built-in effect pipeline creation failed";
    return out;
  }

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = 2048;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(state->device->CreateDescriptorHeap(
          &heap_desc, IID_PPV_ARGS(&state->heap)))) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "D3D12 built-in effect descriptor heap creation failed";
    return out;
  }
  state->descriptor_size = state->device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  out.dispatch = [state](void* native_list, const NativeEffectPass& pass,
                         void* native_input, void* native_output,
                         std::string& diagnostic) {
    std::lock_guard<std::mutex> lock(state->mutex);
    auto* list = static_cast<ID3D12GraphicsCommandList*>(native_list);
    auto* input = static_cast<ID3D12Resource*>(native_input);
    auto* output = static_cast<ID3D12Resource*>(native_output);
    if (!list || !input || !output || input == output) {
      diagnostic = "D3D12 built-in effect dispatch received invalid resources";
      return false;
    }
    const auto in_desc = input->GetDesc();
    const auto out_desc = output->GetDesc();
    if (in_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        out_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        in_desc.Width != out_desc.Width || in_desc.Height != out_desc.Height ||
        resource_format(input) != resource_format(output)) {
      diagnostic = "D3D12 built-in effect resource geometry/format mismatch";
      return false;
    }

    std::uint32_t kind{};
    if (!effect_kind(pass.effect.effect_id == "effect.gaussian_blur" ? EffectType::blur :
                     pass.effect.effect_id == "effect.sharpen" ? EffectType::sharpen :
                     pass.effect.effect_id == "effect.glow" ? EffectType::glow :
                     pass.effect.effect_id == "effect.lens_distortion" ? EffectType::lens_distortion :
                     pass.effect.effect_id == "effect.noise" ? EffectType::noise :
                     pass.effect.effect_id == "effect.film_grain" ? EffectType::film_grain :
                     pass.effect.effect_id == "effect.chromatic_aberration" ? EffectType::chromatic_aberration :
                     pass.effect.effect_id == "effect.vignette" ? EffectType::vignette :
                     pass.effect.effect_id == "effect.motion_blur" ? EffectType::motion_blur :
                     static_cast<EffectType>(999), kind)) {
      diagnostic = "D3D12 built-in effect shader does not recognize effect id";
      return false;
    }

    if (state->cursor + 2 > 2048) state->cursor = 0;
    const UINT slot = state->cursor;
    state->cursor += 2;
    auto cpu = state->heap->GetCPUDescriptorHandleForHeapStart();
    auto gpu = state->heap->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(slot) * state->descriptor_size;
    gpu.ptr += static_cast<UINT64>(slot) * state->descriptor_size;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = in_desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    state->device->CreateShaderResourceView(input, &srv, cpu);

    D3D12_CPU_DESCRIPTOR_HANDLE uav_cpu = cpu;
    uav_cpu.ptr += state->descriptor_size;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = out_desc.Format;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    state->device->CreateUnorderedAccessView(output, nullptr, &uav, uav_cpu);

    ShaderConstants constants{};
    constants.kind = kind;
    constants.width = static_cast<std::uint32_t>(in_desc.Width);
    constants.height = in_desc.Height;
    constants.pass_index = pass.pass_index;
    constants.amount = pass.effect.amount;
    constants.radius = pass.effect.radius;
    constants.angle = pass.effect.angle;
    constants.seed_lo = static_cast<std::uint32_t>(pass.effect.seed);
    constants.seed_hi = static_cast<std::uint32_t>(pass.effect.seed >> 32);
    constants.quality = static_cast<std::uint32_t>(pass.quality);

    ID3D12DescriptorHeap* heaps[] = {state->heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(state->root_signature.Get());
    list->SetPipelineState(state->pipeline.Get());
    list->SetComputeRootDescriptorTable(0, gpu);
    list->SetComputeRoot32BitConstants(1, sizeof(constants) / 4, &constants, 0);
    list->Dispatch((constants.width + 7) / 8, (constants.height + 7) / 8, 1);
    return true;
  };

  out.lifetime = state;
  out.package_identity = "digitor.windows.d3d12.builtin-effects.hlsl-sm5.1.v1";
  out.result = DIGITOR_RESULT_OK;
  return out;
}

}  // namespace digitor

#else

namespace digitor {
WindowsD3D12BuiltinEffectShadersResult
create_windows_d3d12_builtin_effect_shaders(void*) noexcept {
  WindowsD3D12BuiltinEffectShadersResult out{};
  out.result = DIGITOR_RESULT_UNSUPPORTED;
  out.diagnostic = "D3D12 built-in effect shaders are only available on Windows";
  return out;
}
}  // namespace digitor

#endif
