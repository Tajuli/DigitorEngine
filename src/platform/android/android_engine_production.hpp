#pragma once

#include "digitor/android_native_provider.hpp"
#include "digitor/flutter_production_provider_builder.hpp"
#include "digitor/production_integration_runtime.hpp"
#include "gpu/backend_production_capability.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace digitor {
struct AndroidEngineProductionDependencies final {
  ProductionTimelineGpuHost timeline;
  AndroidHardwareEncoderHost encoder;
  AndroidNativeProviderCapabilities capabilities;
  ProductionDecoderFactory decoder_factory;
  ProductionTimestampFrameResolver frame_resolver;
  ProductionTextureDescriptorBuilder texture_descriptor_builder;
  ProductionPreviewTargetBinder preview_target_binder;
  DigitorNativePreviewCapabilities preview_capabilities{};
  std::function<DigitorResult(const FlutterProductionPluginAttachment&,
                              const ProcessedGpuFramePtr&, std::uint64_t,
                              std::string&)> flutter_present;
  EncoderBackend encoder_backend{EncoderBackend::quick_sync};
  std::int32_t fps_num{30};
  std::int32_t fps_den{1};
  std::int64_t video_bitrate{12'000'000};
  std::string package_identity;
  std::string build_identity;
};
using AndroidEngineProductionDependenciesFactory = std::function<std::optional<
    AndroidEngineProductionDependencies>(const BackendProductionCapability&,
        const FlutterProductionPluginAttachment&, std::string&)>;
DigitorResult install_android_engine_production_dependencies_factory(
    AndroidEngineProductionDependenciesFactory, std::string* diagnostic=nullptr) noexcept;
DigitorResult clear_android_engine_production_dependencies_factory() noexcept;
[[nodiscard]] std::unique_ptr<ProductionIntegrationRuntime>
install_android_engine_production_runtime(const BackendProductionCapability&,
                                           std::string* diagnostic=nullptr) noexcept;
struct AndroidEngineProductionBuildResult final {
  std::optional<FlutterProductionProviderBuild> build;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result==DIGITOR_RESULT_OK && build.has_value(); }
};
[[nodiscard]] AndroidEngineProductionBuildResult assemble_android_engine_production_build(
    const BackendProductionCapability&, const FlutterProductionPluginAttachment&,
    AndroidEngineProductionDependencies) noexcept;
} // namespace digitor
