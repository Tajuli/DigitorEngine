#pragma once

#include "digitor/native_platform_provider_fixed.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace digitor {

struct AppleFlutterTextureBridge final {
  const void* flutter_texture_registrar{};
  std::string implementation_identity;
  std::function<bool()> attached;
  std::function<DigitorResult(const ProcessedGpuFramePtr&, std::uint64_t)> present;
};

struct AppleNativeProviderCapabilities final {
  bool metal{};
  bool core_video{};
  bool iosurface{};
  bool video_toolbox{};
  bool hardware_encoder{};
  bool iosurface_pixel_buffer_pool{};
  bool flutter_texture_bridge{};
  bool metal_completion_sync{};
  bool color_attachments{};
  bool hdr_attachments{};
  bool zero_cpu_readback{};
  bool zero_cpu_staging{};
  bool physical_device{};
  std::string device_identity;
  std::string encoder_identity;
};

struct AppleNativeProviderBindings final {
  ProductionPlatform platform{ProductionPlatform::macos};
  ProductionTimelineGpuHost timeline;
  AppleFlutterTextureBridge flutter;
  AppleHardwareEncoderHost encoder;
  AppleNativeProviderCapabilities capabilities;
  const void* device_identity{};
  std::string package_identity;
  std::string build_identity;
};

struct AppleNativeProviderResult final {
  NativePlatformProvider provider;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] AppleNativeProviderResult create_apple_native_provider(
    AppleNativeProviderBindings bindings) noexcept;

}  // namespace digitor
