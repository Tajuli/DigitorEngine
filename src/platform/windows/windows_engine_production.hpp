#pragma once

#include "digitor/flutter_production_provider_builder.hpp"
#include "digitor/windows_native_provider.hpp"
#include "gpu/backend_production_capability.hpp"
#include "digitor/production_integration_runtime.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace digitor {

// Complete Windows dependencies owned by DigitorEngine/platform code. The
// consuming Flutter app never supplies these callbacks: the only app/plugin
// input is the opaque registrar carried by FlutterProductionPluginAttachment.
struct WindowsEngineProductionDependencies final {
  ProductionTimelineGpuHost timeline;
  WindowsHardwareEncoderHost encoder;
  WindowsVulkanZeroCopyInterop vulkan_interop;
  WindowsNativeProviderCapabilities capabilities;
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

using WindowsEngineProductionDependenciesFactory = std::function<std::optional<
    WindowsEngineProductionDependencies>(
        const BackendProductionCapability&,
        const FlutterProductionPluginAttachment&, std::string&)>;

// Validates the strict, engine-owned D3D12 preview seam used by the
// preview-only provider builder. Kept as an internal platform contract so the
// individual bootstrap failure is observable and regression-testable.
[[nodiscard]] bool validate_windows_d3d12_preview_build_inputs(
    const BackendProductionCapability& backend,
    const FlutterProductionPluginAttachment& attachment,
    std::string& diagnostic) noexcept;

// Internal Windows platform seam. The factory is owned by DigitorEngine's
// native Windows integration, never by the consuming Flutter application.
DigitorResult install_windows_engine_production_dependencies_factory(
    WindowsEngineProductionDependenciesFactory factory,
    std::string* diagnostic = nullptr) noexcept;
DigitorResult clear_windows_engine_production_dependencies_factory() noexcept;

[[nodiscard]] std::unique_ptr<ProductionIntegrationRuntime>
install_windows_engine_production_runtime(
    const BackendProductionCapability& backend,
    std::string* diagnostic = nullptr) noexcept;

struct WindowsEngineProductionBuildResult final {
  std::optional<FlutterProductionProviderBuild> build;
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  [[nodiscard]] explicit operator bool() const noexcept {
    return result == DIGITOR_RESULT_OK && build.has_value();
  }
};

// Binds one selected backend generation to the concrete Windows provider. The
// backend capability is authoritative: dependency callbacks cannot substitute
// another device/context or turn CPU/unsupported resources into production.
[[nodiscard]] WindowsEngineProductionBuildResult
assemble_windows_engine_production_build(
    const BackendProductionCapability& backend,
    const FlutterProductionPluginAttachment& attachment,
    WindowsEngineProductionDependencies dependencies) noexcept;

}  // namespace digitor
