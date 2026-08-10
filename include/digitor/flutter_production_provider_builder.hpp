#pragma once

#include "digitor/flutter_production_plugin_bootstrap.hpp"
#include "digitor/native_platform_provider.hpp"

#include <functional>
#include <optional>
#include <string>

namespace digitor {

struct FlutterProductionProviderBuild final {
  NativePlatformProvider provider;
  ProductionPlatformFactoryInputs platform_inputs;
  ProductionDecoderFactory decoder_factory;
  ProductionTimestampFrameResolver frame_resolver;
  ProductionTextureDescriptorBuilder texture_descriptor_builder;
  ProductionPreviewTargetBinder preview_target_binder;
  DigitorNativePreviewCapabilities preview_capabilities{};
  EncoderBackend encoder_backend{EncoderBackend::software};
  std::int32_t fps_num{30};
  std::int32_t fps_den{1};
  std::int64_t video_bitrate{12'000'000};
  std::uint64_t required_device_identity{};
  std::uint64_t required_context_identity{};
};

using FlutterProductionProviderBuildFactory = std::function<
    std::optional<FlutterProductionProviderBuild>(
        const FlutterProductionPluginAttachment&, std::string& diagnostic)>;

// Installs the final engine-owned bridge between a concrete native platform
// provider and Flutter's process-wide production-host registration. The build
// factory is evaluated lazily at plugin attachment time so it can bind the
// actual Flutter registrar and the already-selected renderer/device context.
DigitorResult install_flutter_production_provider_builder(
    DigitorFlutterProductionPluginPlatform platform,
    FlutterProductionProviderBuildFactory factory,
    std::string* diagnostic = nullptr) noexcept;

// Removes a builder installed by the engine-owned production runtime.  Active
// plugin registration is never disturbed; shutdown must first close sessions
// and detach the Flutter plugin.
DigitorResult uninstall_flutter_production_provider_builder(
    DigitorFlutterProductionPluginPlatform platform) noexcept;

[[nodiscard]] bool flutter_production_provider_builder_installed(
    DigitorFlutterProductionPluginPlatform platform) noexcept;

}  // namespace digitor
