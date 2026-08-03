#include "digitor/windows_native_provider.hpp"

#include <memory>
#include <utility>

namespace digitor {
namespace {

[[nodiscard]] NativeImplementationEvidence make_evidence(
    const std::string& identity) {
  NativeImplementationEvidence value{};
  value.production_implementation = true;
  value.native_api_bound = true;
  value.synchronization_bound = true;
  value.zero_copy_telemetry_bound = true;
  value.implementation_identity = identity;
  return value;
}

}  // namespace

WindowsNativeProviderValidation validate_windows_native_provider_inputs(
    const WindowsNativeProviderInputs& inputs) noexcept {
  if (inputs.package_identity.empty() || inputs.build_identity.empty()) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "Windows provider package and build identities are required"};
  }
  if (!inputs.timeline.context_identity || inputs.timeline.device_identity.empty() ||
      !inputs.timeline.create_target || !inputs.timeline.execute_effects ||
      !inputs.timeline.composite_layer || !inputs.timeline.frame_evictable) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "Windows timeline native host is incomplete"};
  }
  if (inputs.timeline.backend != DIGITOR_RENDERER_D3D12 &&
      inputs.timeline.backend != DIGITOR_RENDERER_VULKAN) {
    return {DIGITOR_RESULT_UNSUPPORTED,
            "Windows provider requires D3D12 or Vulkan"};
  }

  const auto& texture = inputs.flutter_texture;
  if (texture.implementation_identity.empty() || !texture.d3d_device_identity ||
      !texture.attached || !texture.register_or_present ||
      !texture.accepts_d3d12_resource || !texture.retains_until_consumed ||
      !texture.native_fence_wait_bound || !texture.zero_cpu_readback ||
      !texture.zero_cpu_staging) {
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
            "Windows external GPU texture registrar is incomplete"};
  }
  if (inputs.timeline.backend == DIGITOR_RENDERER_VULKAN &&
      !texture.accepts_vulkan_external_memory) {
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
            "Windows Vulkan preview requires external-memory texture support"};
  }
  if (inputs.timeline.context_identity != texture.d3d_device_identity) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "timeline and Flutter texture registrar device identities differ"};
  }

  const auto& encoder = inputs.encoder;
  if (encoder.implementation_identity.empty() || !encoder.host.open ||
      !encoder.host.submit || !encoder.host.drain ||
      !encoder.host.finalize_atomic || !encoder.host.cancel ||
      !encoder.host.qualification || !encoder.media_foundation_bound ||
      !encoder.nvenc_or_qsv_bound || !encoder.d3d12_resource_input ||
      !encoder.native_fence_wait_bound || !encoder.zero_cpu_readback ||
      !encoder.zero_cpu_staging) {
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
            "Windows native hardware encoder host is incomplete"};
  }

  if (inputs.timeline.backend == DIGITOR_RENDERER_VULKAN &&
      !validate_windows_vulkan_zero_copy_interop(inputs.vulkan_interop)) {
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
            "Windows Vulkan/DXGI zero-copy interop is incomplete"};
  }

  if (inputs.timeline.device_identity == texture.implementation_identity ||
      inputs.timeline.device_identity == encoder.implementation_identity ||
      texture.implementation_identity == encoder.implementation_identity) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "Windows native component identities must be distinct"};
  }

  return {DIGITOR_RESULT_OK, "Windows native provider inputs valid"};
}

NativePlatformProvider create_windows_native_platform_provider(
    WindowsNativeProviderInputs inputs) {
  NativePlatformProvider provider{};
  provider.platform = ProductionPlatform::windows;
  provider.package_identity = inputs.package_identity;
  provider.build_identity = inputs.build_identity;

  const auto validation = validate_windows_native_provider_inputs(inputs);
  if (!validation) return provider;

  provider.timeline = make_evidence(inputs.timeline.device_identity);
  provider.flutter_texture =
      make_evidence(inputs.flutter_texture.implementation_identity);
  provider.encoder = make_evidence(inputs.encoder.implementation_identity);

  auto retained =
      std::make_shared<WindowsNativeProviderInputs>(std::move(inputs));
  provider.create = [retained](ProductionPlatformFactoryInputs runtime) {
    runtime.platform = ProductionPlatform::windows;
    runtime.timeline = retained->timeline;
    runtime.timeline_evidence = make_evidence(retained->timeline.device_identity);

    runtime.flutter.platform = ProductionPlatform::windows;
    runtime.flutter.backend = retained->timeline.backend;
    runtime.flutter.device_identity = retained->flutter_texture.d3d_device_identity;
    runtime.flutter.device_name = retained->timeline.device_identity;
    runtime.flutter.evidence =
        make_evidence(retained->flutter_texture.implementation_identity);
    runtime.flutter.attached = retained->flutter_texture.attached;
    runtime.flutter.register_or_present =
        retained->flutter_texture.register_or_present;

    runtime.encoder.evidence =
        make_evidence(retained->encoder.implementation_identity);
    runtime.encoder.windows = retained->encoder.host;
    runtime.windows_vulkan = retained->vulkan_interop;

    auto assembly = create_production_platform_assembly(std::move(runtime));
    if (assembly && !assembly.release_ready()) {
      assembly.diagnostic =
          "Windows assembly is functional but lacks production-native evidence";
    }
    return assembly;
  };
  return provider;
}

}  // namespace digitor
