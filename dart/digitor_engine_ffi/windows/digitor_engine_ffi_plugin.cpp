#include "digitor_engine_ffi_plugin.h"

#include <flutter/standard_method_codec.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <dxgi.h>
#include <windows.h>
#include <unknwn.h>
#include <wrl/client.h>

#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace digitor_engine_ffi {
namespace {

constexpr char kChannelName[] = "digitor_engine_ffi/platform_host";
constexpr std::int64_t kDxgiSharedHandle = 1;
constexpr std::int64_t kD3d11Texture = 2;
constexpr std::int64_t kD3d12Backend = 2;
constexpr std::int64_t kReady = 1;
constexpr std::int64_t kRgba8 = 2;
constexpr std::int64_t kBgra8 = 3;

using Map = flutter::EncodableMap;
using Value = flutter::EncodableValue;
using Microsoft::WRL::ComPtr;

const Map* Arguments(const flutter::MethodCall<Value>& call) {
  return call.arguments() ? std::get_if<Map>(call.arguments()) : nullptr;
}

std::optional<std::int64_t> ReadInt(const Map& map, const char* key) {
  const auto it = map.find(Value(key));
  if (it == map.end()) return std::nullopt;
  if (const auto* value = std::get_if<std::int32_t>(&it->second)) return *value;
  if (const auto* value = std::get_if<std::int64_t>(&it->second)) return *value;
  return std::nullopt;
}

std::optional<bool> ReadBool(const Map& map, const char* key) {
  const auto it = map.find(Value(key));
  if (it == map.end()) return std::nullopt;
  if (const auto* value = std::get_if<bool>(&it->second)) return *value;
  return std::nullopt;
}

flutter::EncodableList SupportedHandleTypes() {
  return flutter::EncodableList{Value(kDxgiSharedHandle), Value(kD3d11Texture)};
}

FlutterDesktopPixelFormat FlutterPixelFormat(std::int64_t pixel_format) {
  if (pixel_format == kRgba8) return kFlutterDesktopPixelFormatRGBA8888;
  if (pixel_format == kBgra8) return kFlutterDesktopPixelFormatBGRA8888;
  return kFlutterDesktopPixelFormatNone;
}

DXGI_FORMAT DxgiFormat(FlutterDesktopPixelFormat pixel_format) {
  if (pixel_format == kFlutterDesktopPixelFormatRGBA8888) {
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  }
  if (pixel_format == kFlutterDesktopPixelFormatBGRA8888) {
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  }
  return DXGI_FORMAT_UNKNOWN;
}

std::string HResultDiagnostic(const char* operation, HRESULT hr) {
  char code[16]{};
  std::snprintf(code, sizeof(code), "0x%08lX",
                static_cast<unsigned long>(
                    static_cast<std::uint32_t>(hr)));
  return std::string(operation) + " failed: HRESULT=" + code;
}

std::string LuidDiagnostic(LUID luid) {
  char value[32]{};
  std::snprintf(
      value, sizeof(value), "%08lX:%08lX",
      static_cast<unsigned long>(static_cast<std::uint32_t>(luid.HighPart)),
      static_cast<unsigned long>(luid.LowPart));
  return value;
}

struct ScopedWin32Handle {
  HANDLE value{};
  ~ScopedWin32Handle() {
    if (value) CloseHandle(value);
  }
  ScopedWin32Handle() = default;
  ScopedWin32Handle(const ScopedWin32Handle&) = delete;
  ScopedWin32Handle& operator=(const ScopedWin32Handle&) = delete;
};

struct HandleLease {
  enum class Kind { kNone, kWin32Handle, kComObject } kind{Kind::kNone};
  void* value{};

  ~HandleLease() {
    if (!value) return;
    if (kind == Kind::kWin32Handle) {
      CloseHandle(static_cast<HANDLE>(value));
    } else if (kind == Kind::kComObject) {
      static_cast<IUnknown*>(value)->Release();
    }
    value = nullptr;
    kind = Kind::kNone;
  }
};

void ReleaseLease(void* context) {
  delete static_cast<HandleLease*>(context);
}

std::unique_ptr<HandleLease> RetainD3d11Texture(
    std::uint64_t native_handle) {
  if (!native_handle) return nullptr;
  auto* object = reinterpret_cast<IUnknown*>(native_handle);
  object->AddRef();
  auto lease = std::make_unique<HandleLease>();
  lease->kind = HandleLease::Kind::kComObject;
  lease->value = object;
  return lease;
}

void ReleaseOwnedLease(std::unique_ptr<HandleLease>& lease) {
  lease.reset();
}

}  // namespace

struct DigitorEngineFfiPlugin::D3D11PreviewBridge {
  explicit D3D11PreviewBridge(flutter::PluginRegistrarWindows* registrar) {
    if (!registrar) {
      diagnostic = "Flutter Windows registrar is unavailable";
      return;
    }

    // Flutter 3.44.x exposes the graphics adapter on FlutterView rather than
    // PluginRegistrarWindows. GetGraphicsAdapter returns an AddRef'd adapter;
    // Attach transfers that reference to ComPtr for deterministic release.
    auto* view = registrar->GetView();
    if (!view) {
      diagnostic = "Flutter Windows implicit view is unavailable";
      return;
    }
    IDXGIAdapter* raw_adapter = view->GetGraphicsAdapter();
    if (!raw_adapter) {
      diagnostic = "Flutter Windows graphics adapter is unavailable";
      return;
    }
    ComPtr<IDXGIAdapter> adapter;
    adapter.Attach(raw_adapter);
    DXGI_ADAPTER_DESC adapter_desc{};
    if (FAILED(adapter->GetDesc(&adapter_desc))) {
      diagnostic = "Flutter Windows graphics adapter description is unavailable";
      return;
    }
    flutter_adapter_luid = adapter_desc.AdapterLuid;

    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL selected_level{};
    ComPtr<ID3D11Device> base_device;
    ComPtr<ID3D11DeviceContext> base_context;
    HRESULT hr = D3D11CreateDevice(
        adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
        static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        base_device.GetAddressOf(), &selected_level,
        base_context.GetAddressOf());
    if (hr == E_INVALIDARG) {
      hr = D3D11CreateDevice(
          adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
          D3D11_CREATE_DEVICE_BGRA_SUPPORT, &levels[1], 1,
          D3D11_SDK_VERSION, base_device.GetAddressOf(), &selected_level,
          base_context.GetAddressOf());
    }
    if (FAILED(hr)) {
      diagnostic = HResultDiagnostic("D3D11CreateDevice(Flutter adapter)", hr);
      return;
    }
    hr = base_device.As(&device);
    if (FAILED(hr)) {
      diagnostic = HResultDiagnostic("QueryInterface(ID3D11Device1)", hr);
      return;
    }
    context = std::move(base_context);

    // Create a D3D12 device on the exact adapter Flutter renders with. This is
    // both a deterministic adapter-identity probe and the producer of a clean
    // cross-API compatibility carrier. The engine remains the production
    // renderer; this device only performs GPU-to-GPU presentation copies.
    hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                           IID_PPV_ARGS(d3d12_device.GetAddressOf()));
    if (FAILED(hr) || !d3d12_device) {
      diagnostic = HResultDiagnostic("D3D12CreateDevice(Flutter adapter)", hr);
      return;
    }
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = d3d12_device->CreateCommandQueue(
        &queue_desc, IID_PPV_ARGS(d3d12_queue.GetAddressOf()));
    if (FAILED(hr)) {
      diagnostic = HResultDiagnostic("ID3D12Device::CreateCommandQueue", hr);
      return;
    }
    hr = d3d12_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(d3d12_allocator.GetAddressOf()));
    if (FAILED(hr)) {
      diagnostic = HResultDiagnostic("ID3D12Device::CreateCommandAllocator", hr);
      return;
    }
    hr = d3d12_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, d3d12_allocator.Get(), nullptr,
        IID_PPV_ARGS(d3d12_list.GetAddressOf()));
    if (FAILED(hr)) {
      diagnostic = HResultDiagnostic("ID3D12Device::CreateCommandList", hr);
      return;
    }
    hr = d3d12_list->Close();
    if (FAILED(hr)) {
      diagnostic = HResultDiagnostic("ID3D12GraphicsCommandList::Close", hr);
      return;
    }
    hr = d3d12_device->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(d3d12_fence.GetAddressOf()));
    if (FAILED(hr)) {
      diagnostic = HResultDiagnostic("ID3D12Device::CreateFence", hr);
      return;
    }
    d3d12_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!d3d12_fence_event) {
      diagnostic = "CreateEvent(D3D12 Flutter bridge fence) failed";
      return;
    }
    diagnostic.clear();
  }

  ~D3D11PreviewBridge() {
    if (d3d12_fence_event) CloseHandle(d3d12_fence_event);
  }

  bool ready() const noexcept {
    return device && context && d3d12_device && d3d12_queue &&
           d3d12_allocator && d3d12_list && d3d12_fence &&
           d3d12_fence_event;
  }

  HRESULT BeginD3d12Commands() {
    HRESULT hr = d3d12_allocator->Reset();
    if (FAILED(hr)) return hr;
    return d3d12_list->Reset(d3d12_allocator.Get(), nullptr);
  }

  HRESULT SubmitD3d12AndWait() {
    HRESULT hr = d3d12_list->Close();
    if (FAILED(hr)) return hr;
    ID3D12CommandList* lists[]{d3d12_list.Get()};
    d3d12_queue->ExecuteCommandLists(1, lists);
    const UINT64 value = ++d3d12_fence_value;
    hr = d3d12_queue->Signal(d3d12_fence.Get(), value);
    if (FAILED(hr)) return hr;
    if (d3d12_fence->GetCompletedValue() < value) {
      hr = d3d12_fence->SetEventOnCompletion(value, d3d12_fence_event);
      if (FAILED(hr)) return hr;
      if (WaitForSingleObject(d3d12_fence_event, INFINITE) != WAIT_OBJECT_0) {
        return HRESULT_FROM_WIN32(GetLastError());
      }
    }
    return S_OK;
  }

  bool CopyD3d12NtHandleToLegacySurface(
      std::uint64_t source_handle,
      std::size_t width,
      std::size_t height,
      FlutterDesktopPixelFormat pixel_format,
      std::unique_ptr<HandleLease>& destination_lease,
      HANDLE& legacy_shared_handle,
      std::string& error) {
    std::scoped_lock lock(mutex);
    destination_lease.reset();
    legacy_shared_handle = nullptr;
    if (!ready()) {
      error = diagnostic.empty()
                  ? "Flutter D3D11/D3D12 preview bridge is unavailable"
                  : diagnostic;
      return false;
    }
    if (!source_handle || width == 0 || height == 0) {
      error = "Flutter preview bridge received an invalid D3D12 surface";
      return false;
    }

    const auto expected_format = DxgiFormat(pixel_format);
    if (expected_format == DXGI_FORMAT_UNKNOWN) {
      error = "Flutter preview bridge received an unsupported pixel format";
      return false;
    }

    const auto nt_handle = reinterpret_cast<HANDLE>(source_handle);
    ComPtr<ID3D12Resource> engine_source;
    HRESULT hr = d3d12_device->OpenSharedHandle(
        nt_handle, IID_PPV_ARGS(engine_source.GetAddressOf()));
    if (FAILED(hr) || !engine_source) {
      error = HResultDiagnostic(
                  "ID3D12Device::OpenSharedHandle(engine preview on Flutter adapter)",
                  hr) +
              "; FlutterAdapterLuid=" + LuidDiagnostic(flutter_adapter_luid) +
              "; the engine preview handle is not openable on Flutter's "
              "graphics adapter (adapter mismatch or invalid NT handle)";
      return false;
    }

    const auto source12_desc = engine_source->GetDesc();
    if (source12_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        source12_desc.Width != width || source12_desc.Height != height ||
        source12_desc.MipLevels != 1 || source12_desc.DepthOrArraySize != 1 ||
        source12_desc.SampleDesc.Count != 1 ||
        source12_desc.Format != expected_format) {
      error = "Engine D3D12 preview resource metadata is incompatible with "
              "the Flutter bridge";
      return false;
    }

    // Do not ask D3D11 to open the engine's UAV-capable shader output directly.
    // D3D12/D3D11 interop requires a D3D11-compatible resource description.
    // Copy into a dedicated, minimal RGBA carrier whose only purpose is
    // cross-API sharing. ALLOW_SIMULTANEOUS_ACCESS mirrors the semantics D3D11
    // gives shared resources; no CPU-visible heap or CPU copy is introduced.
    D3D12_RESOURCE_DESC carrier_desc = source12_desc;
    carrier_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> carrier;
    hr = d3d12_device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_SHARED, &carrier_desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(carrier.GetAddressOf()));
    if (FAILED(hr) || !carrier) {
      error = HResultDiagnostic(
          "ID3D12Device::CreateCommittedResource(Flutter compatibility carrier)",
          hr);
      return false;
    }

    hr = BeginD3d12Commands();
    if (FAILED(hr)) {
      error = HResultDiagnostic("Begin D3D12 Flutter carrier copy", hr);
      return false;
    }
    D3D12_RESOURCE_BARRIER source_to_copy{};
    source_to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    source_to_copy.Transition.pResource = engine_source.Get();
    source_to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    source_to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    source_to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    d3d12_list->ResourceBarrier(1, &source_to_copy);
    d3d12_list->CopyResource(carrier.Get(), engine_source.Get());

    D3D12_RESOURCE_BARRIER finish[2]{};
    finish[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    finish[0].Transition.pResource = engine_source.Get();
    finish[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    finish[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    finish[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    finish[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    finish[1].Transition.pResource = carrier.Get();
    finish[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    finish[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    finish[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    d3d12_list->ResourceBarrier(2, finish);
    hr = SubmitD3d12AndWait();
    if (FAILED(hr)) {
      error = HResultDiagnostic("Submit D3D12 Flutter carrier copy", hr);
      return false;
    }

    ScopedWin32Handle carrier_handle;
    hr = d3d12_device->CreateSharedHandle(
        carrier.Get(), nullptr, GENERIC_ALL, nullptr, &carrier_handle.value);
    if (FAILED(hr) || !carrier_handle.value) {
      error = HResultDiagnostic(
          "ID3D12Device::CreateSharedHandle(Flutter compatibility carrier)", hr);
      return false;
    }

    ComPtr<ID3D11Texture2D> source;
    hr = device->OpenSharedResource1(
        carrier_handle.value, IID_PPV_ARGS(source.GetAddressOf()));
    if (FAILED(hr) || !source) {
      error = HResultDiagnostic(
                  "ID3D11Device1::OpenSharedResource1(D3D12 compatibility carrier)",
                  hr) +
              "; same-adapter D3D12 OpenSharedHandle succeeded; "
              "carrier=RGBA8+SHARED+ALLOW_SIMULTANEOUS_ACCESS";
      return false;
    }

    D3D11_TEXTURE2D_DESC source_desc{};
    source->GetDesc(&source_desc);
    if (source_desc.Width != width || source_desc.Height != height ||
        source_desc.MipLevels != 1 || source_desc.ArraySize != 1 ||
        source_desc.SampleDesc.Count != 1 ||
        source_desc.Format != expected_format) {
      error = "D3D12 compatibility carrier is incompatible with the Flutter "
              "D3D11 bridge";
      return false;
    }

    D3D11_TEXTURE2D_DESC destination_desc{};
    destination_desc.Width = static_cast<UINT>(width);
    destination_desc.Height = static_cast<UINT>(height);
    destination_desc.MipLevels = 1;
    destination_desc.ArraySize = 1;
    destination_desc.Format = expected_format;
    destination_desc.SampleDesc.Count = 1;
    destination_desc.Usage = D3D11_USAGE_DEFAULT;
    destination_desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    destination_desc.CPUAccessFlags = 0;
    // Flutter's Windows DXGI external-texture path uses ANGLE's legacy
    // EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE contract. That contract is fed by
    // IDXGIResource::GetSharedHandle, not by an NT handle from
    // ID3D12Device::CreateSharedHandle.
    destination_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    ComPtr<ID3D11Texture2D> destination;
    hr = device->CreateTexture2D(&destination_desc, nullptr,
                                 destination.GetAddressOf());
    if (FAILED(hr) || !destination) {
      error = HResultDiagnostic(
          "ID3D11Device::CreateTexture2D(Flutter legacy shared surface)", hr);
      return false;
    }

    // Keep the final adaptation entirely GPU-side. Microsoft requires Flush on
    // a D3D11 device after updating a shared texture before another device uses
    // it. No Map/staging/readback path exists here.
    context->CopyResource(destination.Get(), source.Get());
    context->Flush();

    ComPtr<IDXGIResource> shared_resource;
    hr = destination.As(&shared_resource);
    if (FAILED(hr) || !shared_resource) {
      error = HResultDiagnostic("QueryInterface(IDXGIResource)", hr);
      return false;
    }
    HANDLE shared_handle = nullptr;
    hr = shared_resource->GetSharedHandle(&shared_handle);
    if (FAILED(hr) || !shared_handle) {
      error = HResultDiagnostic("IDXGIResource::GetSharedHandle", hr);
      return false;
    }

    auto lease = std::make_unique<HandleLease>();
    lease->kind = HandleLease::Kind::kComObject;
    lease->value = destination.Detach();
    destination_lease = std::move(lease);
    legacy_shared_handle = shared_handle;
    error.clear();
    return true;
  }

  ComPtr<ID3D11Device1> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D12Device> d3d12_device;
  ComPtr<ID3D12CommandQueue> d3d12_queue;
  ComPtr<ID3D12CommandAllocator> d3d12_allocator;
  ComPtr<ID3D12GraphicsCommandList> d3d12_list;
  ComPtr<ID3D12Fence> d3d12_fence;
  HANDLE d3d12_fence_event{};
  UINT64 d3d12_fence_value{};
  LUID flutter_adapter_luid{};
  std::mutex mutex;
  std::string diagnostic;
};

struct DigitorEngineFfiPlugin::TextureState {
  std::mutex mutex;
  std::int64_t handle_type{};
  std::unique_ptr<HandleLease> retained_handle;
  HANDLE flutter_shared_handle{};
  std::size_t width{};
  std::size_t height{};
  FlutterDesktopPixelFormat pixel_format{kFlutterDesktopPixelFormatNone};
  std::uint64_t generation{};
  std::uint64_t device_identity{};
  std::uint64_t context_identity{};
  FlutterDesktopGpuSurfaceDescriptor descriptor{};
  std::unique_ptr<flutter::TextureVariant> texture;

  ~TextureState() { ReleaseOwnedLease(retained_handle); }

  const FlutterDesktopGpuSurfaceDescriptor* ObtainDescriptor(
      std::size_t requested_width, std::size_t requested_height) {
    std::scoped_lock lock(mutex);
    if (!retained_handle || !retained_handle->value || !generation ||
        !width || !height)
      return nullptr;
    if (requested_width && requested_width != width) return nullptr;
    if (requested_height && requested_height != height) return nullptr;

    auto lease = std::make_unique<HandleLease>();
    void* flutter_handle = nullptr;
    if (handle_type == kDxgiSharedHandle) {
      if (!flutter_shared_handle) return nullptr;
      // GetSharedHandle returns a legacy DXGI shared handle. It is deliberately
      // not duplicated or closed. Keep the creator texture alive instead until
      // Flutter confirms that it has opened the handle.
      auto* object = static_cast<IUnknown*>(retained_handle->value);
      object->AddRef();
      lease->kind = HandleLease::Kind::kComObject;
      lease->value = object;
      flutter_handle = flutter_shared_handle;
    } else if (handle_type == kD3d11Texture) {
      auto* object = static_cast<IUnknown*>(retained_handle->value);
      object->AddRef();
      lease->kind = HandleLease::Kind::kComObject;
      lease->value = object;
      flutter_handle = object;
    } else {
      return nullptr;
    }

    descriptor = {};
    descriptor.struct_size = sizeof(descriptor);
    descriptor.handle = flutter_handle;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.visible_width = width;
    descriptor.visible_height = height;
    descriptor.format = pixel_format;
    descriptor.release_callback = ReleaseLease;
    descriptor.release_context = lease.release();
    return &descriptor;
  }
};

void DigitorEngineFfiPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto plugin = std::make_unique<DigitorEngineFfiPlugin>(registrar);
  registrar->AddPlugin(std::move(plugin));
}

DigitorEngineFfiPlugin::DigitorEngineFfiPlugin(
    flutter::PluginRegistrarWindows* registrar)
    : texture_registrar_(registrar->texture_registrar()),
      d3d11_preview_bridge_(std::make_unique<D3D11PreviewBridge>(registrar)) {
  channel_ = std::make_unique<flutter::MethodChannel<Value>>(
      registrar->messenger(), kChannelName,
      &flutter::StandardMethodCodec::GetInstance());
  channel_->SetMethodCallHandler(
      [this](const flutter::MethodCall<Value>& call,
             std::unique_ptr<flutter::MethodResult<Value>> result) {
        HandleMethodCall(call, std::move(result));
      });
}

DigitorEngineFfiPlugin::~DigitorEngineFfiPlugin() {
  std::vector<std::int64_t> ids;
  {
    std::scoped_lock lock(mutex_);
    for (const auto& [id, _] : textures_) ids.push_back(id);
  }
  for (const auto id : ids) DisposeTexture(id);
}

void DigitorEngineFfiPlugin::DisposeTexture(std::int64_t texture_id) {
  std::shared_ptr<TextureState> state;
  {
    std::scoped_lock lock(mutex_);
    const auto it = textures_.find(texture_id);
    if (it == textures_.end()) return;
    state = std::move(it->second);
    textures_.erase(it);
  }
  if (texture_registrar_) {
    texture_registrar_->UnregisterTexture(texture_id, [state]() mutable {
      state.reset();
    });
  }
}

void DigitorEngineFfiPlugin::HandleMethodCall(
    const flutter::MethodCall<Value>& call,
    std::unique_ptr<flutter::MethodResult<Value>> result) {
  if (call.method_name() == "capabilities") {
    Map response;
    response[Value("platform")] = Value("windows");
    response[Value("supportedHandleTypes")] = Value(SupportedHandleTypes());
    response[Value("directDescriptorPresentation")] = Value(true);
    response[Value("renderTargetPresentation")] = Value(false);
    result->Success(Value(response));
    return;
  }

  if (call.method_name() == "productionRegistrarToken") {
    if (!texture_registrar_) {
      result->Error("registrar_unavailable",
                    "Flutter Windows texture registrar is unavailable.");
      return;
    }
    result->Success(Value(static_cast<std::int64_t>(
        reinterpret_cast<std::uintptr_t>(texture_registrar_))));
    return;
  }

  const auto* args = Arguments(call);
  if (!args) {
    result->Error("invalid_arguments", "Expected a map of arguments.");
    return;
  }

  if (call.method_name() == "createTexture") {
    const auto handle_type = ReadInt(*args, "handleType");
    const auto width = ReadInt(*args, "width");
    const auto height = ReadInt(*args, "height");
    if (!handle_type || !width || !height || *width <= 0 || *height <= 0 ||
        (*handle_type != kDxgiSharedHandle && *handle_type != kD3d11Texture)) {
      result->Error("unsupported_texture",
                    "Windows requires a DXGI shared handle or D3D11 texture.");
      return;
    }

    auto state = std::make_shared<TextureState>();
    state->handle_type = *handle_type;
    state->width = static_cast<std::size_t>(*width);
    state->height = static_cast<std::size_t>(*height);
    const auto surface_type = *handle_type == kDxgiSharedHandle
                                  ? kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle
                                  : kFlutterDesktopGpuSurfaceTypeD3d11Texture2D;
    std::weak_ptr<TextureState> weak = state;
    state->texture = std::make_unique<flutter::TextureVariant>(
        flutter::GpuSurfaceTexture(
            surface_type,
            [weak](std::size_t w, std::size_t h)
                -> const FlutterDesktopGpuSurfaceDescriptor* {
              const auto state = weak.lock();
              return state ? state->ObtainDescriptor(w, h) : nullptr;
            }));

    const auto texture_id = texture_registrar_->RegisterTexture(state->texture.get());
    if (texture_id < 0) {
      result->Error("registration_failed",
                    "Flutter rejected the Windows GPU surface texture.");
      return;
    }
    {
      std::scoped_lock lock(mutex_);
      textures_[texture_id] = state;
    }
    Map response;
    response[Value("textureId")] = Value(texture_id);
    response[Value("nativeTargetHandle")] = Value(std::int64_t{0});
    response[Value("targetKind")] = Value("windows-gpu-surface");
    result->Success(Value(response));
    return;
  }

  const auto texture_id = ReadInt(*args, "textureId");
  if (!texture_id) {
    result->Error("invalid_texture", "textureId is required.");
    return;
  }

  if (call.method_name() == "disposeTexture") {
    DisposeTexture(*texture_id);
    result->Success();
    return;
  }

  std::shared_ptr<TextureState> state;
  {
    std::scoped_lock lock(mutex_);
    const auto it = textures_.find(*texture_id);
    if (it != textures_.end()) state = it->second;
  }
  if (!state) {
    result->Error("invalid_texture", "Unknown Flutter texture id.");
    return;
  }

  if (call.method_name() == "present") {
    const auto backend = ReadInt(*args, "backend");
    const auto handle_type = ReadInt(*args, "handleType");
    const auto native_handle = ReadInt(*args, "nativeHandle");
    const auto width = ReadInt(*args, "width");
    const auto height = ReadInt(*args, "height");
    const auto generation = ReadInt(*args, "generation");
    const auto readiness = ReadInt(*args, "readiness");
    const auto pixel_format = ReadInt(*args, "pixelFormat");
    const auto device_identity = ReadInt(*args, "deviceIdentity");
    const auto context_identity = ReadInt(*args, "contextIdentity");
    const auto protected_content = ReadBool(*args, "protectedContent").value_or(false);
    if (!backend || !handle_type || !native_handle || !width || !height ||
        !generation || !readiness || !pixel_format || *native_handle == 0 ||
        *generation <= 0 || *readiness != kReady || protected_content ||
        *handle_type != state->handle_type || *width <= 0 || *height <= 0) {
      result->Error("incompatible_frame",
                    "Frame is stale, protected, not ready, or incompatible with the registered texture.");
      return;
    }
    const auto flutter_pixel_format = FlutterPixelFormat(*pixel_format);
    if (flutter_pixel_format == kFlutterDesktopPixelFormatNone) {
      result->Error("unsupported_pixel_format",
                    "Windows GPU surface requires RGBA8 or BGRA8.");
      return;
    }

    {
      std::scoped_lock lock(state->mutex);
      if (static_cast<std::uint64_t>(*generation) <= state->generation) {
        result->Error("stale_generation", "Preview generations must increase.");
        return;
      }
      if (state->device_identity && device_identity &&
          static_cast<std::uint64_t>(*device_identity) != state->device_identity) {
        result->Error("device_mismatch", "Preview device identity changed.");
        return;
      }
      if (state->context_identity && context_identity &&
          static_cast<std::uint64_t>(*context_identity) != state->context_identity) {
        result->Error("context_mismatch", "Preview context identity changed.");
        return;
      }
    }

    std::unique_ptr<HandleLease> retained;
    HANDLE flutter_shared_handle = nullptr;
    if (*handle_type == kDxgiSharedHandle) {
      if (*backend != kD3d12Backend) {
        result->Error("unsupported_dxgi_producer",
                      "Windows DXGI preview bridge currently requires a D3D12 producer.");
        return;
      }
      std::string bridge_error;
      if (!d3d11_preview_bridge_ ||
          !d3d11_preview_bridge_->CopyD3d12NtHandleToLegacySurface(
              static_cast<std::uint64_t>(*native_handle),
              static_cast<std::size_t>(*width),
              static_cast<std::size_t>(*height), flutter_pixel_format,
              retained, flutter_shared_handle, bridge_error)) {
        result->Error("d3d12_flutter_bridge_failed", bridge_error);
        return;
      }
    } else {
      retained = RetainD3d11Texture(static_cast<std::uint64_t>(*native_handle));
      if (!retained) {
        result->Error(
            "frame_handle_retain_failed",
            "Windows could not retain the D3D11 preview texture before the engine released the preview generation.");
        return;
      }
    }

    std::unique_ptr<HandleLease> previous_handle;
    {
      std::scoped_lock lock(state->mutex);
      // Recheck after the GPU bridge work in case a newer frame won the race.
      if (static_cast<std::uint64_t>(*generation) <= state->generation) {
        result->Error("stale_generation", "Preview generations must increase.");
        return;
      }
      previous_handle = std::move(state->retained_handle);
      state->retained_handle = std::move(retained);
      state->flutter_shared_handle = flutter_shared_handle;
      state->width = static_cast<std::size_t>(*width);
      state->height = static_cast<std::size_t>(*height);
      state->generation = static_cast<std::uint64_t>(*generation);
      state->device_identity =
          device_identity ? static_cast<std::uint64_t>(*device_identity) : 0;
      state->context_identity =
          context_identity ? static_cast<std::uint64_t>(*context_identity) : 0;
      state->pixel_format = flutter_pixel_format;
    }
    ReleaseOwnedLease(previous_handle);
    if (!texture_registrar_->MarkTextureFrameAvailable(*texture_id)) {
      result->Error("frame_signal_failed",
                    "Flutter texture registrar rejected the frame signal.");
      return;
    }
    result->Success();
    return;
  }

  if (call.method_name() == "markFrameAvailable") {
    if (!texture_registrar_->MarkTextureFrameAvailable(*texture_id)) {
      result->Error("frame_signal_failed",
                    "Flutter texture registrar rejected the frame signal.");
      return;
    }
    result->Success();
    return;
  }

  result->NotImplemented();
}

}  // namespace digitor_engine_ffi