#pragma once

#include "digitor/native_platform_provider_fixed.hpp"

#include <functional>
#include <string>

namespace digitor {

struct AppleFlutterTextureBridge final {
  const void* texture_registrar{};
  std::string implementation_identity;
  std::function<bool()> attached;
  std::function<bool()> lifecycle_ready;
  std::function<DigitorResult(const ProcessedGpuFramePtr&, std::uint64_t)> present;
};

struct AppleNativeProviderCapabilities final {
  ApplePlatform platform{ApplePlatform::macos};
  bool metal{};
  bool iosurface{};
  bool cvpixelbuffer{};
  bool video_toolbox{};
  bool hardware_encoder{};
  bool flutter_texture_bridge{};
  bool lifecycle_safe{};
  bool zero_cpu_readback{};
  bool zero_cpu_staging{};
  std::string device_identity;
  std::string encoder_identity;
};

struct AppleNativeProviderBindings final {
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
