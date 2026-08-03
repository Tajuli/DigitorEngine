#include "digitor/windows_native_provider.hpp"

#include <utility>

namespace digitor {
namespace {

NativeImplementationEvidence evidence(std::string identity) {
  NativeImplementationEvidence out{};
  out.production_implementation = true;
  out.native_api_bound = true;
  out.synchronization_bound = true;
  out.zero_copy_telemetry_bound = true;
  out.implementation_identity = std::move(identity);
  return out;
}

bool timeline_host_valid(const ProductionTimelineGpuHost& host) {
  return host.backend != DIGITOR_RENDERER_CPU && host.context_identity != nullptr &&
         host.working_format == DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT &&
         !host.device_identity.empty() && host.create_target &&
         host.execute_effects && host.composite_layer && host.frame_evictable;
}

}  // namespace

WindowsNativeProviderValidation validate_windows_native_provider_inputs(
    const WindowsNativeProviderInputs& inputs) noexcept {
  if (inputs.package_identity.empty() || inputs.build_identity.empty()) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "Windows provider package/build identity is required"};
  }
  if (inputs.timeline.backend != DIGITOR_RENDERER_D3D12 &&
      inputs.timeline.backend != DIGITOR_RENDERER_VULKAN) {
    return {DIGITOR_RESULT_UNSUPPORTED,
            "Windows provider requires D3D12 or Vulkan timeline backend"};
  }
  if (!timeline_host_valid(inputs.timeline)) {
    return {DIGITOR_RESULT_NOT_INITIALIZED,
            "Windows production timeline host is incomplete"};
  }

  const auto& texture = inputs.flutter_texture;
  if (texture.implementation_identity.empty() || !texture.d3d_device_identity ||
      !texture.attached || !texture.register_or_present ||
      !texture.accepts_d3d12_resource || !texture.retains_until_consumed ||
      !texture.native_fence_wait_bound || !texture.zero_cpu_readback ||
      !texture.zero_cpu_staging) {
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
            "external GPU Flutter texture implementation is incomplete"};
  }
  if (inputs.timeline.backend == DIGITOR_RENDERER_VULKAN &&
      !texture.accepts_vulkan_external_memory) {
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
            "Vulkan preview requires external-memory Flutter texture support"};
  }
  if (inputs.timeline.context_identity != texture.d3d_device_identity) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "timeline and Flutter texture device identities differ"};
  }

  const auto& encoder = inputs.encoder;
  if (encoder.implementation_identity.empty() || !encoder.media_foundation_bound ||
      !encoder.d3d12_resource_input || !encoder.native_fence_wait_bound ||
      !encoder.zero_cpu_readback || !encoder.zero_cpu_staging ||
      !encoder.host.open || !encoder.host.submit || !encoder.host.drain ||
      !encoder.host.finalize_atomic || !encoder.host.cancel ||
      !encoder.host.qualification) {
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
            "native Windows hardware encoder implementation is incomplete"};
  }

  if (inputs.timeline.backend == DIGITOR_RENDERER_VULKAN &&
      !validate_windows_vulkan_zero_copy_interop(inputs.vulkan_interop)) {
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
            "Windows Vulkan/DXGI zero-copy interop is incomplete"};
  }
  return {DIGITOR_RESULT_OK, "Windows production-native provider inputs valid"};
}

NativePlatformProvider create_windows_native_platform_provider(
    WindowsNativeProviderInputs inputs) {
  NativePlatformProvider provider{};
  provider.platform = ProductionPlatform::windows;
  provider.package_identity = inputs.package_identity;
  provider.build_identity = inputs.build_identity;

  const auto validation = validate_windows_native_provider_inputs(inputs);
  if (!validation) return provider;

  provider.timeline = evidence("windows.timeline." + inputs.timeline.device_identity);
  provider.flutter_texture =
      evidence("windows.flutter." + inputs.flutter_texture.implementation_identity);
  provider.encoder = evidence("windows.encoder." + inputs.encoder.implementation_identity);

  provider.create = [inputs = std::move(inputs)](
                        ProductionPlatformFactoryInputs factory) mutable {
    factory.platform = ProductionPlatform::windows;
    factory.timeline = inputs.timeline;
    factory.timeline_evidence = evidence("windows.timeline." + inputs.timeline.device_identity);

    factory.flutter.platform = ProductionPlatform::windows;
    factory.flutter.backend = inputs.timeline.backend;
    factory.flutter.device_identity = inputs.flutter_texture.d3d_device_identity;
    factory.flutter.device_name = inputs.timeline.device_identity;
    factory.flutter.evidence = evidence(
        "windows.flutter." + inputs.flutter_texture.implementation_identity);
    factory.flutter.attached = inputs.flutter_texture.attached;
    factory.flutter.register_or_present = inputs.flutter_texture.register_or_present;

    factory.encoder.evidence =
        evidence("windows.encoder." + inputs.encoder.implementation_identity);
    factory.encoder.windows = inputs.encoder.host;
    factory.windows_vulkan = inputs.vulkan_interop;
    return create_production_platform_assembly(std::move(factory));
  };
  return provider;
}

}  // namespace digitor
