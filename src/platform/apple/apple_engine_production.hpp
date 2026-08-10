#pragma once

#include "digitor/apple_native_provider.hpp"
#include "digitor/flutter_production_provider_builder.hpp"
#include "digitor/production_integration_runtime.hpp"
#include "gpu/backend_production_capability.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace digitor {
struct AppleEngineProductionDependencies final {
  ProductionPlatform platform{ProductionPlatform::macos};
  ProductionTimelineGpuHost timeline;
  AppleHardwareEncoderHost encoder;
  AppleNativeProviderCapabilities capabilities;
  ProductionDecoderFactory decoder_factory;
  ProductionTimestampFrameResolver frame_resolver;
  ProductionTextureDescriptorBuilder texture_descriptor_builder;
  ProductionPreviewTargetBinder preview_target_binder;
  DigitorNativePreviewCapabilities preview_capabilities{};
  std::function<DigitorResult(const FlutterProductionPluginAttachment&,
                              const ProcessedGpuFramePtr&, std::uint64_t,
                              std::string&)> flutter_present;
  EncoderBackend encoder_backend{EncoderBackend::video_toolbox};
  std::int32_t fps_num{30};
  std::int32_t fps_den{1};
  std::int64_t video_bitrate{12'000'000};
  std::string package_identity;
  std::string build_identity;
};
using AppleEngineProductionDependenciesFactory = std::function<std::optional<
    AppleEngineProductionDependencies>(const BackendProductionCapability&,
        const FlutterProductionPluginAttachment&, std::string&)>;
DigitorResult install_apple_engine_production_dependencies_factory(
    AppleEngineProductionDependenciesFactory, std::string* diagnostic=nullptr) noexcept;
DigitorResult clear_apple_engine_production_dependencies_factory() noexcept;
[[nodiscard]] std::unique_ptr<ProductionIntegrationRuntime>
install_apple_engine_production_runtime(DigitorFlutterProductionPluginPlatform,
    const BackendProductionCapability&, std::string* diagnostic=nullptr) noexcept;
struct AppleEngineProductionBuildResult final {
  std::optional<FlutterProductionProviderBuild> build;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result==DIGITOR_RESULT_OK && build.has_value(); }
};
[[nodiscard]] AppleEngineProductionBuildResult assemble_apple_engine_production_build(
    DigitorFlutterProductionPluginPlatform, const BackendProductionCapability&,
    const FlutterProductionPluginAttachment&, AppleEngineProductionDependencies) noexcept;
} // namespace digitor
