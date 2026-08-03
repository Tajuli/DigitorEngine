#pragma once

#include "digitor/native_platform_provider_fixed.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace digitor {

// Implemented by the Flutter Android plugin with the real TextureRegistry /
// SurfaceTexture or ImageTexture APIs. It receives GPU-owned frames only.
struct AndroidFlutterTextureBridge final {
  const void* flutter_texture_registrar{};
  std::string implementation_identity;
  std::function<bool()> attached;
  std::function<DigitorResult(const ProcessedGpuFramePtr&, std::uint64_t)> present;
};

struct AndroidNativeProviderCapabilities final {
  bool android_ndk{};
  bool api_26_or_newer{};
  bool mediacodec_ndk{};
  bool native_window{};
  bool ahardwarebuffer{};
  bool flutter_texture_bridge{};
  bool vulkan_external_memory_ahb{};
  bool vulkan_external_semaphore_fd{};
  bool gles_eglimage{};
  bool gles_native_fence_sync{};
  bool hardware_encoder{};
  bool input_surface{};
  bool zero_cpu_readback{};
  bool zero_cpu_staging{};
  std::string device_identity;
  std::string codec_identity;
};

struct AndroidNativeProviderBindings final {
  ProductionTimelineGpuHost timeline;
  AndroidFlutterTextureBridge flutter;
  AndroidHardwareEncoderHost encoder;
  AndroidNativeProviderCapabilities capabilities;
  const void* device_identity{};
  std::string package_identity;
  std::string build_identity;
};

struct AndroidNativeProviderResult final {
  NativePlatformProvider provider;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] AndroidNativeProviderResult create_android_native_provider(
    AndroidNativeProviderBindings bindings) noexcept;

}  // namespace digitor
