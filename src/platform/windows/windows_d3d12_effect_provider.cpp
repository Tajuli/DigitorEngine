#include "digitor/windows_d3d12_effect_provider.hpp"

#if defined(_WIN32)

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <atomic>
#include <limits>
#include <mutex>
#include <utility>

namespace digitor {
namespace {

using Microsoft::WRL::ComPtr;

DXGI_FORMAT d3d12_effect_format(NativeEffectFormat format) noexcept {
  switch (format) {
    case NativeEffectFormat::rgba8_unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case NativeEffectFormat::rgba16_float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case NativeEffectFormat::bgra8_unorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
  }
}

struct D3D12EffectState final {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  HANDLE event_handle{};
  std::uint64_t fence_value{};
  std::uint64_t identity{};
  std::string shader_identity;
  WindowsD3D12EffectDispatch dispatch;
  std::mutex mutex;
  bool recording{};

  ~D3D12EffectState() {
    if (event_handle) CloseHandle(event_handle);
  }

  bool begin(std::string& diagnostic) {
    if (recording) return true;
    if (FAILED(allocator->Reset())) {
      diagnostic = "D3D12 effect command allocator reset failed";
      return false;
    }
    if (FAILED(list->Reset(allocator.Get(), nullptr))) {
      diagnostic = "D3D12 effect command list reset failed";
      return false;
    }
    recording = true;
    return true;
  }

  bool submit_and_wait(std::string& diagnostic) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!recording) {
      diagnostic = "D3D12 effect submission has no recorded passes";
      return false;
    }
    if (FAILED(list->Close())) {
      recording = false;
      diagnostic = "D3D12 effect command list close failed";
      return false;
    }
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    const std::uint64_t value = ++fence_value;
    if (FAILED(queue->Signal(fence.Get(), value))) {
      recording = false;
      diagnostic = "D3D12 effect queue fence signal failed";
      return false;
    }
    if (fence->GetCompletedValue() < value) {
      if (FAILED(fence->SetEventOnCompletion(value, event_handle))) {
        recording = false;
        diagnostic = "D3D12 effect fence event registration failed";
        return false;
      }
      const DWORD wait = WaitForSingleObject(event_handle, 30000);
      if (wait != WAIT_OBJECT_0) {
        recording = false;
        diagnostic = "D3D12 effect GPU submission timed out";
        return false;
      }
    }
    recording = false;
    return true;
  }
};

void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  if (before == after) return;
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &barrier);
}

}  // namespace

WindowsD3D12EffectProviderResult create_windows_d3d12_effect_provider(
    WindowsD3D12EffectProviderBindings bindings) noexcept {
  WindowsD3D12EffectProviderResult out{};
  if (!bindings.device || !bindings.command_queue || !bindings.device_identity ||
      bindings.shader_package_identity.empty() || !bindings.dispatch) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "D3D12 effect provider bindings are incomplete";
    return out;
  }
  if (!bindings.supports_external_memory ||
      !bindings.supports_external_synchronization) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "D3D12 effect provider requires zero-copy interop and synchronization";
    return out;
  }

  auto state = std::make_shared<D3D12EffectState>();
  state->device = static_cast<ID3D12Device*>(bindings.device);
  state->queue = static_cast<ID3D12CommandQueue*>(bindings.command_queue);
  state->identity = bindings.device_identity;
  state->shader_identity = std::move(bindings.shader_package_identity);
  state->dispatch = std::move(bindings.dispatch);

  if (FAILED(state->device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&state->allocator))) ||
      FAILED(state->device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, state->allocator.Get(), nullptr,
          IID_PPV_ARGS(&state->list))) ||
      FAILED(state->list->Close()) ||
      FAILED(state->device->CreateFence(
          0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&state->fence)))) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "D3D12 effect command resources could not be created";
    return out;
  }
  state->event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!state->event_handle) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "D3D12 effect fence event could not be created";
    return out;
  }

  NativeEffectBackendProvider provider{};
  provider.backend = NativeEffectBackend::d3d12;
  provider.device_identity = bindings.device_identity;
  provider.supports_external_memory = true;
  provider.supports_external_synchronization = true;
  provider.supports_hdr = bindings.supports_hdr;
  provider.pass_count = [](const EffectDescriptor& descriptor,
                           const EffectInstance&, EffectQuality) {
    switch (descriptor.type) {
      case EffectType::blur:
      case EffectType::glow:
      case EffectType::motion_blur:
        return 2u;
      default:
        return 1u;
    }
  };
  provider.allocate_transient = [state](const NativeEffectSurface& prototype,
                                        NativeEffectSurface& output,
                                        std::string& diagnostic) {
    const DXGI_FORMAT format = d3d12_effect_format(prototype.format);
    if (format == DXGI_FORMAT_UNKNOWN) {
      diagnostic = "unsupported D3D12 effect transient format";
      return false;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = prototype.width;
    desc.Height = prototype.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* resource = nullptr;
    const HRESULT hr = state->device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(hr) || !resource) {
      diagnostic = "D3D12 effect transient texture allocation failed";
      return false;
    }
    output = prototype;
    output.texture_handle = reinterpret_cast<std::uint64_t>(resource);
    output.device_identity = state->identity;
    output.engine_owned = true;
    output.external_memory = false;
    output.cpu_mappable = false;
    return true;
  };
  provider.release_transient = [](const NativeEffectSurface& surface) {
    if (surface.engine_owned && surface.texture_handle) {
      reinterpret_cast<ID3D12Resource*>(surface.texture_handle)->Release();
    }
  };
  provider.record_pass = [state](const NativeEffectPass& pass,
                                 std::string& diagnostic) {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->begin(diagnostic)) return false;
    auto* input = reinterpret_cast<ID3D12Resource*>(pass.input.texture_handle);
    auto* output = reinterpret_cast<ID3D12Resource*>(pass.output.texture_handle);
    if (!input || !output || input == output) {
      diagnostic = "invalid D3D12 effect pass resources";
      return false;
    }
    transition(state->list.Get(), input, D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(state->list.Get(), output, D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!state->dispatch(state->list.Get(), pass, input, output, diagnostic)) {
      return false;
    }
    D3D12_RESOURCE_BARRIER uav{};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav.UAV.pResource = output;
    state->list->ResourceBarrier(1, &uav);
    transition(state->list.Get(), input,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_COMMON);
    transition(state->list.Get(), output,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_COMMON);
    return true;
  };
  provider.submit = [state](std::string& diagnostic) {
    return state->submit_and_wait(diagnostic);
  };

  std::string diagnostic;
  if (!validate_native_effect_provider(provider, diagnostic)) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = std::move(diagnostic);
    return out;
  }
  out.provider = std::move(provider);
  out.lifetime = std::move(state);
  out.result = DIGITOR_RESULT_OK;
  return out;
}

}  // namespace digitor

#else

namespace digitor {
WindowsD3D12EffectProviderResult create_windows_d3d12_effect_provider(
    WindowsD3D12EffectProviderBindings) noexcept {
  WindowsD3D12EffectProviderResult out{};
  out.result = DIGITOR_RESULT_UNSUPPORTED;
  out.diagnostic = "D3D12 effect provider is only available on Windows";
  return out;
}
}  // namespace digitor

#endif
