#include "digitor_engine_ffi_plugin.h"

#include <flutter/standard_method_codec.h>
#include <windows.h>
#include <unknwn.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace digitor_engine_ffi {
namespace {

constexpr char kChannelName[] = "digitor_engine_ffi/platform_host";
constexpr std::int64_t kDxgiSharedHandle = 1;
constexpr std::int64_t kD3d11Texture = 2;
constexpr std::int64_t kReady = 1;
constexpr std::int64_t kRgba8 = 2;
constexpr std::int64_t kBgra8 = 3;

using Map = flutter::EncodableMap;
using Value = flutter::EncodableValue;

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

struct HandleLease {
  enum class Kind { kNone, kWin32Handle, kComObject } kind{Kind::kNone};
  void* value{};
};

void ReleaseLease(void* context) {
  std::unique_ptr<HandleLease> lease(static_cast<HandleLease*>(context));
  if (!lease || !lease->value) return;
  if (lease->kind == HandleLease::Kind::kWin32Handle) {
    CloseHandle(static_cast<HANDLE>(lease->value));
  } else if (lease->kind == HandleLease::Kind::kComObject) {
    static_cast<IUnknown*>(lease->value)->Release();
  }
}

}  // namespace

struct DigitorEngineFfiPlugin::TextureState {
  std::mutex mutex;
  std::int64_t handle_type{};
  std::uint64_t native_handle{};
  std::size_t width{};
  std::size_t height{};
  FlutterDesktopPixelFormat pixel_format{kFlutterDesktopPixelFormatNone};
  std::uint64_t generation{};
  std::uint64_t device_identity{};
  std::uint64_t context_identity{};
  FlutterDesktopGpuSurfaceDescriptor descriptor{};
  std::unique_ptr<flutter::TextureVariant> texture;

  const FlutterDesktopGpuSurfaceDescriptor* ObtainDescriptor(
      std::size_t requested_width, std::size_t requested_height) {
    std::scoped_lock lock(mutex);
    if (!native_handle || !generation || !width || !height) return nullptr;
    if (requested_width && requested_width != width) return nullptr;
    if (requested_height && requested_height != height) return nullptr;

    auto lease = std::make_unique<HandleLease>();
    void* flutter_handle = nullptr;
    if (handle_type == kDxgiSharedHandle) {
      HANDLE duplicate = nullptr;
      if (!DuplicateHandle(GetCurrentProcess(),
                           reinterpret_cast<HANDLE>(native_handle),
                           GetCurrentProcess(), &duplicate, 0, FALSE,
                           DUPLICATE_SAME_ACCESS)) {
        return nullptr;
      }
      lease->kind = HandleLease::Kind::kWin32Handle;
      lease->value = duplicate;
      flutter_handle = duplicate;
    } else if (handle_type == kD3d11Texture) {
      auto* object = reinterpret_cast<IUnknown*>(native_handle);
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
    : texture_registrar_(registrar->texture_registrar()) {
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
    if (!handle_type || !native_handle || !width || !height || !generation ||
        !readiness || !pixel_format || *native_handle == 0 || *generation <= 0 ||
        *readiness != kReady || protected_content ||
        *handle_type != state->handle_type || *width <= 0 || *height <= 0) {
      result->Error("incompatible_frame",
                    "Frame is stale, protected, not ready, or incompatible with the registered texture.");
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
      state->native_handle = static_cast<std::uint64_t>(*native_handle);
      state->width = static_cast<std::size_t>(*width);
      state->height = static_cast<std::size_t>(*height);
      state->generation = static_cast<std::uint64_t>(*generation);
      state->device_identity = device_identity ? static_cast<std::uint64_t>(*device_identity) : 0;
      state->context_identity = context_identity ? static_cast<std::uint64_t>(*context_identity) : 0;
      state->pixel_format = FlutterPixelFormat(*pixel_format);
    }
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
