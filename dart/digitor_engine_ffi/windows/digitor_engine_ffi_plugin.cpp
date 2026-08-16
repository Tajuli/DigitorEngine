#include "digitor_engine_ffi_plugin.h"

#include <flutter/standard_method_codec.h>
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <d3dcompiler.h>
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

std::string BlobMessage(ID3DBlob* blob) {
  if (!blob || !blob->GetBufferPointer() || !blob->GetBufferSize()) return {};
  return std::string(static_cast<const char*>(blob->GetBufferPointer()),
                     blob->GetBufferSize());
}

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

    HRESULT hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                   IID_PPV_ARGS(d3d12_device.GetAddressOf()));
    if (FAILED(hr) || !d3d12_device) {
      diagnostic = HResultDiagnostic("D3D12CreateDevice(Flutter adapter)", hr);
      return;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = d3d12_device->CreateCommandQueue(
        &queue_desc, IID_PPV_ARGS(d3d12_queue.GetAddressOf()));
    if (FAILED(hr) || !d3d12_queue) {
      diagnostic = HResultDiagnostic("ID3D12Device::CreateCommandQueue", hr);
      return;
    }

    IUnknown* queues[]{d3d12_queue.Get()};
    D3D_FEATURE_LEVEL selected_level{};
    hr = D3D11On12CreateDevice(
        d3d12_device.Get(), D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, queues, 1, 0,
        d3d11_device.GetAddressOf(), &d3d11_context, &selected_level);
    if (FAILED(hr) || !d3d11_device || !d3d11_context) {
      diagnostic = HResultDiagnostic(
          "D3D11On12CreateDevice(Flutter D3D12 bridge)", hr);
      return;
    }
    hr = d3d11_device.As(&d3d11on12_device);
    if (FAILED(hr) || !d3d11on12_device) {
      diagnostic = HResultDiagnostic("QueryInterface(ID3D11On12Device)", hr);
      return;
    }

    // Flutter's own Windows GPU-surface tests create the DXGI shared-handle
    // texture as BGRA8 on a native D3D11 device. Keep a native D3D11 device on
    // the exact Flutter adapter as an explicit compatibility probe before a
    // handle is handed to ANGLE.
    const D3D_FEATURE_LEVEL probe_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL probe_level{};
    ComPtr<ID3D11DeviceContext> probe_context;
    hr = D3D11CreateDevice(
        adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, probe_levels,
        static_cast<UINT>(std::size(probe_levels)), D3D11_SDK_VERSION,
        native_probe_device.GetAddressOf(), &probe_level,
        probe_context.GetAddressOf());
    if (hr == E_INVALIDARG) {
      hr = D3D11CreateDevice(
          adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
          D3D11_CREATE_DEVICE_BGRA_SUPPORT, &probe_levels[1], 1,
          D3D11_SDK_VERSION, native_probe_device.GetAddressOf(), &probe_level,
          probe_context.GetAddressOf());
    }
    if (FAILED(hr) || !native_probe_device) {
      diagnostic = HResultDiagnostic("D3D11CreateDevice(Flutter adapter probe)", hr);
      return;
    }

    static constexpr char kVertexShaderSource[] = R"(
float4 main(uint vertex_id : SV_VertexID) : SV_POSITION {
  if (vertex_id == 0) return float4(-1.0,  1.0, 0.0, 1.0);
  if (vertex_id == 1) return float4( 3.0,  1.0, 0.0, 1.0);
  return                    float4(-1.0, -3.0, 0.0, 1.0);
}
)";
    static constexpr char kPixelShaderSource[] = R"(
Texture2D<float4> source_texture : register(t0);
float4 main(float4 position : SV_POSITION) : SV_TARGET {
  return source_texture.Load(int3(int2(position.xy), 0));
}
)";

    ComPtr<ID3DBlob> shader_blob;
    ComPtr<ID3DBlob> shader_errors;
    hr = D3DCompile(kVertexShaderSource, sizeof(kVertexShaderSource) - 1,
                    "DigitorFlutterPreviewVS", nullptr, nullptr, "main",
                    "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                    shader_blob.GetAddressOf(), shader_errors.GetAddressOf());
    if (FAILED(hr)) {
      diagnostic = HResultDiagnostic("D3DCompile(Flutter preview vertex shader)", hr);
      const auto details = BlobMessage(shader_errors.Get());
      if (!details.empty()) diagnostic += "; " + details;
      return;
    }
    hr = d3d11_device->CreateVertexShader(
        shader_blob->GetBufferPointer(), shader_blob->GetBufferSize(), nullptr,
        blit_vertex_shader.GetAddressOf());
    if (FAILED(hr) || !blit_vertex_shader) {
      diagnostic = HResultDiagnostic("ID3D11Device::CreateVertexShader", hr);
      return;
    }

    shader_blob.Reset();
    shader_errors.Reset();
    hr = D3DCompile(kPixelShaderSource, sizeof(kPixelShaderSource) - 1,
                    "DigitorFlutterPreviewPS", nullptr, nullptr, "main",
                    "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
                    shader_blob.GetAddressOf(), shader_errors.GetAddressOf());
    if (FAILED(hr)) {
      diagnostic = HResultDiagnostic("D3DCompile(Flutter preview pixel shader)", hr);
      const auto details = BlobMessage(shader_errors.Get());
      if (!details.empty()) diagnostic += "; " + details;
      return;
    }
    hr = d3d11_device->CreatePixelShader(
        shader_blob->GetBufferPointer(), shader_blob->GetBufferSize(), nullptr,
        blit_pixel_shader.GetAddressOf());
    if (FAILED(hr) || !blit_pixel_shader) {
      diagnostic = HResultDiagnostic("ID3D11Device::CreatePixelShader", hr);
      return;
    }

    diagnostic.clear();
  }

  bool ready() const noexcept {
    return d3d11_device && d3d11_context && d3d11on12_device &&
           d3d12_device && d3d12_queue && native_probe_device &&
           blit_vertex_shader && blit_pixel_shader;
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
                  ? "Flutter D3D11On12 preview bridge is unavailable"
                  : diagnostic;
      return false;
    }
    if (!source_handle || width == 0 || height == 0) {
      error = "Flutter preview bridge received an invalid D3D12 surface";
      return false;
    }

    const auto source_format = DxgiFormat(pixel_format);
    if (source_format == DXGI_FORMAT_UNKNOWN) {
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
        source12_desc.Format != source_format) {
      error = "Engine D3D12 preview resource metadata is incompatible with "
              "the Flutter D3D11On12 bridge";
      return false;
    }

    // Promote the engine's RGBA/BGRA texture into D3D11On12 as a shader
    // resource. The final Flutter surface is always BGRA8 because Flutter's
    // Windows EGL/ANGLE path and its own shared-handle unit test use BGRA8.
    D3D11_RESOURCE_FLAGS wrapped_flags{};
    wrapped_flags.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> wrapped_source;
    hr = d3d11on12_device->CreateWrappedResource(
        engine_source.Get(), &wrapped_flags,
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON,
        IID_PPV_ARGS(wrapped_source.GetAddressOf()));
    if (FAILED(hr) || !wrapped_source) {
      error = HResultDiagnostic(
                  "ID3D11On12Device::CreateWrappedResource(engine preview)", hr) +
              "; FlutterAdapterLuid=" + LuidDiagnostic(flutter_adapter_luid);
      return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = source_format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> source_srv;
    hr = d3d11_device->CreateShaderResourceView(
        wrapped_source.Get(), &srv_desc, source_srv.GetAddressOf());
    if (FAILED(hr) || !source_srv) {
      error = HResultDiagnostic(
          "ID3D11Device(D3D11On12)::CreateShaderResourceView(engine preview)", hr);
      return false;
    }

    D3D11_TEXTURE2D_DESC destination_desc{};
    destination_desc.Width = static_cast<UINT>(width);
    destination_desc.Height = static_cast<UINT>(height);
    destination_desc.MipLevels = 1;
    destination_desc.ArraySize = 1;
    destination_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    destination_desc.SampleDesc.Count = 1;
    destination_desc.Usage = D3D11_USAGE_DEFAULT;
    destination_desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    destination_desc.CPUAccessFlags = 0;
    destination_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    ComPtr<ID3D11Texture2D> destination;
    hr = d3d11_device->CreateTexture2D(
        &destination_desc, nullptr, destination.GetAddressOf());
    if (FAILED(hr) || !destination) {
      error = HResultDiagnostic(
          "ID3D11Device(D3D11On12)::CreateTexture2D(Flutter BGRA shared surface)",
          hr);
      return false;
    }

    ComPtr<ID3D11RenderTargetView> destination_rtv;
    hr = d3d11_device->CreateRenderTargetView(
        destination.Get(), nullptr, destination_rtv.GetAddressOf());
    if (FAILED(hr) || !destination_rtv) {
      error = HResultDiagnostic(
          "ID3D11Device(D3D11On12)::CreateRenderTargetView(Flutter BGRA surface)",
          hr);
      return false;
    }

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    ID3D11RenderTargetView* rtvs[]{destination_rtv.Get()};
    ID3D11ShaderResourceView* srvs[]{source_srv.Get()};
    d3d11_context->OMSetRenderTargets(1, rtvs, nullptr);
    d3d11_context->RSSetViewports(1, &viewport);
    d3d11_context->IASetInputLayout(nullptr);
    d3d11_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d3d11_context->VSSetShader(blit_vertex_shader.Get(), nullptr, 0);
    d3d11_context->PSSetShader(blit_pixel_shader.Get(), nullptr, 0);
    d3d11_context->PSSetShaderResources(0, 1, srvs);
    d3d11_context->Draw(3, 0);

    ID3D11ShaderResourceView* null_srvs[]{nullptr};
    ID3D11RenderTargetView* null_rtvs[]{nullptr};
    d3d11_context->PSSetShaderResources(0, 1, null_srvs);
    d3d11_context->OMSetRenderTargets(1, null_rtvs, nullptr);

    ID3D11Resource* wrapped_resources[]{wrapped_source.Get()};
    d3d11on12_device->ReleaseWrappedResources(wrapped_resources, 1);
    d3d11_context->Flush();

    hr = d3d11_device->GetDeviceRemovedReason();
    if (FAILED(hr)) {
      error = HResultDiagnostic(
          "D3D11On12 device health after Flutter BGRA GPU blit", hr);
      return false;
    }

    ComPtr<IDXGIResource> shared_resource;
    hr = destination.As(&shared_resource);
    if (FAILED(hr) || !shared_resource) {
      error = HResultDiagnostic("QueryInterface(IDXGIResource)", hr);
      return false;
    }

    HANDLE shared_handle = nullptr;
    hr = shared_resource->GetSharedHandle(&shared_handle);
    if (FAILED(hr) || !shared_handle) {
      error = HResultDiagnostic(
          "IDXGIResource::GetSharedHandle(D3D11On12 Flutter BGRA surface)", hr);
      return false;
    }

    // Probe the exact legacy handle through a normal D3D11 device before it is
    // handed to ANGLE. A failure here is actionable and avoids reducing all
    // external-texture errors to Flutter's generic "Binding D3D surface failed".
    ComPtr<ID3D11Texture2D> probe_texture;
    hr = native_probe_device->OpenSharedResource(
        shared_handle, IID_PPV_ARGS(probe_texture.GetAddressOf()));
    if (FAILED(hr) || !probe_texture) {
      error = HResultDiagnostic(
                  "ID3D11Device::OpenSharedResource(Flutter legacy BGRA probe)",
                  hr) +
              "; FlutterAdapterLuid=" + LuidDiagnostic(flutter_adapter_luid);
      return false;
    }
    D3D11_TEXTURE2D_DESC probe_desc{};
    probe_texture->GetDesc(&probe_desc);
    if (probe_desc.Width != width || probe_desc.Height != height ||
        probe_desc.MipLevels != 1 || probe_desc.ArraySize != 1 ||
        probe_desc.SampleDesc.Count != 1 ||
        probe_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
      error = "Flutter legacy BGRA probe opened but returned incompatible "
              "texture metadata";
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

  ComPtr<ID3D11Device> d3d11_device;
  ComPtr<ID3D11DeviceContext> d3d11_context;
  ComPtr<ID3D11On12Device> d3d11on12_device;
  ComPtr<ID3D12Device> d3d12_device;
  ComPtr<ID3D12CommandQueue> d3d12_queue;
  ComPtr<ID3D11Device> native_probe_device;
  ComPtr<ID3D11VertexShader> blit_vertex_shader;
  ComPtr<ID3D11PixelShader> blit_pixel_shader;
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
      std::size_t /*requested_width*/, std::size_t /*requested_height*/) {
    std::scoped_lock lock(mutex);
    if (!retained_handle || !retained_handle->value || !generation ||
        !width || !height)
      return nullptr;

    auto lease = std::make_unique<HandleLease>();
    void* flutter_handle = nullptr;
    if (handle_type == kDxgiSharedHandle) {
      if (!flutter_shared_handle) return nullptr;
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
    const auto protected_content =
        ReadBool(*args, "protectedContent").value_or(false);
    if (!backend || !handle_type || !native_handle || !width || !height ||
        !generation || !readiness || !pixel_format || *native_handle == 0 ||
        *generation <= 0 || *readiness != kReady || protected_content ||
        *handle_type != state->handle_type || *width <= 0 || *height <= 0) {
      result->Error(
          "incompatible_frame",
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
          static_cast<std::uint64_t>(*device_identity) !=
              state->device_identity) {
        result->Error("device_mismatch", "Preview device identity changed.");
        return;
      }
      if (state->context_identity && context_identity &&
          static_cast<std::uint64_t>(*context_identity) !=
              state->context_identity) {
        result->Error("context_mismatch", "Preview context identity changed.");
        return;
      }
    }

    std::unique_ptr<HandleLease> retained;
    HANDLE flutter_shared_handle = nullptr;
    FlutterDesktopPixelFormat presented_pixel_format = flutter_pixel_format;
    if (*handle_type == kDxgiSharedHandle) {
      if (*backend != kD3d12Backend) {
        result->Error(
            "unsupported_dxgi_producer",
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
      presented_pixel_format = kFlutterDesktopPixelFormatBGRA8888;
    } else {
      retained = RetainD3d11Texture(
          static_cast<std::uint64_t>(*native_handle));
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
      state->pixel_format = presented_pixel_format;
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
