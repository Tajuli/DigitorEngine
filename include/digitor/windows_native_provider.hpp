#pragma once

#include "digitor/native_platform_provider.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace digitor {

// Opaque Flutter Windows texture registrar bridge. The Windows Flutter plugin
// owns these exact SDK calls; DigitorEngine never maps the GPU frame to CPU
// pixels. A provider is production-ready only when all functions are bound to
// the real Flutter Windows embedding and the registered texture consumes the
// same D3D12/Vulkan-shared resource.
struct WindowsFlutterGpuTextureApi final {
  void* registrar{};
  std::int64_t (*register_gpu_texture)(void* registrar, void* native_resource,
                                       std::uint32_t width,
                                       std::uint32_t height) noexcept{};
  bool (*mark_frame_available)(void* registrar,
                               std::int64_t texture_id) noexcept{};
  bool (*unregister_texture)(void* registrar,
                             std::int64_t texture_id) noexcept{};

  [[nodiscard]] bool valid() const noexcept {
    return registrar && register_gpu_texture && mark_frame_available &&
           unregister_texture;
  }
};

struct WindowsNativeProviderConfig final {
  DigitorRendererBackend renderer_backend{DIGITOR_RENDERER_D3D12};
  const void* engine_device_identity{};
  std::string adapter_identity;
  WindowsFlutterGpuTextureApi flutter;
  bool require_vulkan_dxgi_interop{};
};

struct WindowsNativeProviderStatus final {
  bool com_initialized{};
  bool dxgi_factory_created{};
  bool d3d12_device_created{};
  bool media_foundation_started{};
  bool adapter_identity_matched{};
  bool flutter_texture_bound{};
  bool hardware_encoder_available{};
  bool external_memory_available{};
  bool external_semaphore_available{};
  bool zero_cpu_readback{true};
  bool zero_cpu_staging{true};
  std::string diagnostic;

  [[nodiscard]] bool production_ready(bool vulkan_required) const noexcept {
    const bool common = com_initialized && dxgi_factory_created &&
                        d3d12_device_created && media_foundation_started &&
                        adapter_identity_matched && flutter_texture_bound &&
                        hardware_encoder_available && zero_cpu_readback &&
                        zero_cpu_staging && diagnostic.empty();
    return common && (!vulkan_required ||
                      (external_memory_available &&
                       external_semaphore_available));
  }
};

class WindowsNativeProviderRuntime final {
 public:
  explicit WindowsNativeProviderRuntime(WindowsNativeProviderConfig config);
  ~WindowsNativeProviderRuntime();

  WindowsNativeProviderRuntime(const WindowsNativeProviderRuntime&) = delete;
  WindowsNativeProviderRuntime& operator=(const WindowsNativeProviderRuntime&) = delete;

  [[nodiscard]] const WindowsNativeProviderStatus& status() const noexcept;
  [[nodiscard]] NativePlatformProvider provider() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace digitor
