#ifndef FLUTTER_PLUGIN_DIGITOR_ENGINE_FFI_PLUGIN_H_
#define FLUTTER_PLUGIN_DIGITOR_ENGINE_FFI_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/texture_registrar.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

namespace digitor_engine_ffi {

class DigitorEngineFfiPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

  explicit DigitorEngineFfiPlugin(flutter::PluginRegistrarWindows* registrar);
  ~DigitorEngineFfiPlugin() override;

  DigitorEngineFfiPlugin(const DigitorEngineFfiPlugin&) = delete;
  DigitorEngineFfiPlugin& operator=(const DigitorEngineFfiPlugin&) = delete;

 private:
  struct TextureState;
  struct D3D11PreviewBridge;

  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void DisposeTexture(std::int64_t texture_id);

  flutter::TextureRegistrar* texture_registrar_{};
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;
  std::unique_ptr<D3D11PreviewBridge> d3d11_preview_bridge_;
  std::mutex mutex_;
  std::map<std::int64_t, std::shared_ptr<TextureState>> textures_;
};

}  // namespace digitor_engine_ffi

#endif  // FLUTTER_PLUGIN_DIGITOR_ENGINE_FFI_PLUGIN_H_
