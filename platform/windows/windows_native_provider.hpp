#pragma once

#ifdef _WIN32

#include "digitor/native_platform_provider.hpp"

#include <cstdint>
#include <memory>
#include <string>

struct FlutterDesktopPluginRegistrar;
struct FlutterDesktopTextureRegistrar;
struct ID3D12Device;
struct ID3D12CommandQueue;
struct IDXGIAdapter4;

namespace digitor::windows {

struct WindowsNativeProviderContext final {
  FlutterDesktopPluginRegistrar* plugin_registrar{};
  FlutterDesktopTextureRegistrar* texture_registrar{};
  ID3D12Device* d3d12_device{};
  ID3D12CommandQueue* d3d12_queue{};
  IDXGIAdapter4* dxgi_adapter{};
  const void* renderer_context_identity{};
  std::string adapter_identity;
  bool vulkan_external_memory_available{};
  bool vulkan_external_semaphore_available{};
};

struct WindowsNativeProviderProbe final {
  bool flutter_registrar_bound{};
  bool d3d12_bound{};
  bool media_foundation_available{};
  bool hardware_encoder_available{};
  bool vulkan_dxgi_interop_available{};
  bool zero_copy_supported{};
  std::string diagnostic;
};

[[nodiscard]] WindowsNativeProviderProbe probe_windows_native_provider(
    const WindowsNativeProviderContext& context) noexcept;

[[nodiscard]] NativePlatformProvider create_windows_native_platform_provider(
    std::shared_ptr<WindowsNativeProviderContext> context);

}  // namespace digitor::windows

#endif
