#include "digitor/effect_system.hpp"
#include "digitor/native_effects.hpp"
#include "digitor/windows_d3d12_builtin_effect_shaders.hpp"
#include "digitor/windows_d3d12_effect_provider.hpp"

#if defined(_WIN32)
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

ComPtr<IDXGIAdapter1> choose_adapter(IDXGIFactory6* factory, bool& hardware) {
  ComPtr<IDXGIAdapter1> adapter;
  hardware = false;
  for (UINT index = 0;
       factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                           IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
       ++index) {
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
        SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                    __uuidof(ID3D12Device), nullptr))) {
      hardware = true;
      return adapter;
    }
    adapter.Reset();
  }
  ComPtr<IDXGIAdapter> warp;
  if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) {
    warp.As(&adapter);
  }
  return adapter;
}

ComPtr<ID3D12Resource> create_texture(ID3D12Device* device, DXGI_FORMAT format,
                                      std::uint32_t width, std::uint32_t height) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> resource;
  const HRESULT hr = device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
      nullptr, IID_PPV_ARGS(&resource));
  assert(SUCCEEDED(hr) && resource);
  return resource;
}

digitor::NativeEffectSurface surface(ID3D12Resource* resource,
                                     std::uint64_t device_identity,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     digitor::NativeEffectFormat format) {
  digitor::NativeEffectSurface value{};
  value.texture_handle = reinterpret_cast<std::uint64_t>(resource);
  value.device_identity = device_identity;
  value.width = width;
  value.height = height;
  value.format = format;
  value.engine_owned = false;
  value.external_memory = true;
  value.cpu_mappable = false;
  return value;
}

void run_all_effects(digitor::NativeEffectRuntime& runtime,
                     const digitor::EffectRegistry& registry,
                     ID3D12Device* device,
                     std::uint64_t device_identity,
                     DXGI_FORMAT dxgi_format,
                     digitor::NativeEffectFormat native_format) {
  constexpr std::uint32_t width = 64;
  constexpr std::uint32_t height = 48;
  for (const auto& descriptor : registry.effects()) {
    auto input = create_texture(device, dxgi_format, width, height);
    auto output = create_texture(device, dxgi_format, width, height);
    digitor::EffectStack stack;
    digitor::EffectInstance instance{};
    instance.effect_id = descriptor.id;
    instance.amount = descriptor.default_amount;
    instance.radius = descriptor.default_radius;
    instance.angle = descriptor.default_angle;
    instance.seed = 0x123456789abcdef0ULL;
    assert(stack.add(instance));
    std::string diagnostic;
    const bool ok = runtime.execute(
        registry, stack, digitor::EffectQuality::export_quality,
        surface(input.Get(), device_identity, width, height, native_format),
        surface(output.Get(), device_identity, width, height, native_format),
        &diagnostic);
    if (!ok) {
      std::cerr << descriptor.id << ": " << diagnostic << '\n';
    }
    assert(ok);
  }
}

}  // namespace

int main() {
  ComPtr<IDXGIFactory6> factory;
  assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
  bool hardware = false;
  auto adapter = choose_adapter(factory.Get(), hardware);
  assert(adapter);

  ComPtr<ID3D12Device> device;
  assert(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                     IID_PPV_ARGS(&device))));
  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  assert(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))));

  const auto shaders =
      digitor::create_windows_d3d12_builtin_effect_shaders(device.Get());
  assert(shaders);

  digitor::WindowsD3D12EffectProviderBindings bindings{};
  bindings.device = device.Get();
  bindings.command_queue = queue.Get();
  bindings.device_identity = reinterpret_cast<std::uint64_t>(device.Get());
  bindings.shader_package_identity = shaders.package_identity;
  bindings.dispatch = shaders.dispatch;
  bindings.supports_hdr = true;
  bindings.supports_external_memory = true;
  bindings.supports_external_synchronization = true;
  const auto provider = digitor::create_windows_d3d12_effect_provider(bindings);
  assert(provider);

  digitor::NativeEffectRuntime runtime(provider.provider);
  const digitor::EffectRegistry registry;
  assert(registry.effects().size() == 9);
  run_all_effects(runtime, registry, device.Get(), bindings.device_identity,
                  DXGI_FORMAT_R8G8B8A8_UNORM,
                  digitor::NativeEffectFormat::rgba8_unorm);
  run_all_effects(runtime, registry, device.Get(), bindings.device_identity,
                  DXGI_FORMAT_R16G16B16A16_FLOAT,
                  digitor::NativeEffectFormat::rgba16_float);

  const auto telemetry = runtime.telemetry();
  assert(telemetry.submitted_passes >= 18);
  assert(telemetry.cpu_readbacks == 0);
  assert(telemetry.cpu_reuploads == 0);
  assert(telemetry.fallback_dispatches == 0);

  std::cout << "D3D12_EFFECTS_QUALIFICATION=PASS\n";
  std::cout << "ADAPTER_CLASS=" << (hardware ? "HARDWARE" : "WARP") << '\n';
  std::cout << "BUILTIN_EFFECTS=" << registry.effects().size() << '\n';
  std::cout << "SUBMITTED_PASSES=" << telemetry.submitted_passes << '\n';
  return 0;
}

#else
int main() { return 0; }
#endif
