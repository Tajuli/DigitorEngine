#pragma once

#include "digitor/native_platform_provider.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

// Narrow bridge implemented by the Windows Flutter plugin target. The bridge
// does not expose pixel buffers; it receives the exact GPU frame ownership
// object and must register/present its native D3D12/Vulkan-backed texture.
struct WindowsFlutterTextureBridge final {
  const void* flutter_texture_registrar{};
  std::string implementation_identity;
  std::function<bool()> attached;
  std::function<DigitorResult(const ProcessedGpuFramePtr&, std::uint64_t)> present;
};

struct WindowsNativeProviderCapabilities final {
  bool windows_sdk{};
  bool dxgi_1_6{};
  bool d3d12{};
  bool media_foundation{};
  bool hardware_encoder_mft{};
  bool flutter_texture_bridge{};
  bool vulkan_external_memory{};
  bool vulkan_external_semaphore{};
  bool zero_cpu_readback{};
  bool zero_cpu_staging{};
  std::string adapter_identity;
  std::string encoder_identity;
};

struct WindowsNativeProviderBindings final {
  ProductionTimelineGpuHost timeline;
  WindowsFlutterTextureBridge flutter;
  WindowsHardwareEncoderHost encoder;
  WindowsVulkanZeroCopyInterop vulkan_interop;
  WindowsNativeProviderCapabilities capabilities;
  const void* device_identity{};
  std::string package_identity;
  std::string build_identity;
};

struct WindowsNativeProviderResult final {
  NativePlatformProvider provider;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] WindowsNativeProviderResult create_windows_native_provider(
    WindowsNativeProviderBindings bindings) noexcept;

}  // namespace digitor
