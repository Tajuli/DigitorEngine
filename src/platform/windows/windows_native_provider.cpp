#include "digitor/windows_native_provider.hpp"

#include <utility>

namespace digitor {
namespace {

[[nodiscard]] bool capabilities_valid(
    const WindowsNativeProviderCapabilities& value,
    DigitorRendererBackend backend) noexcept {
  if (!value.windows_sdk || !value.dxgi_1_6 || !value.d3d12 ||
      !value.media_foundation || !value.hardware_encoder_mft ||
      !value.flutter_texture_bridge || !value.zero_cpu_readback ||
      !value.zero_cpu_staging || value.adapter_identity.empty() ||
      value.encoder_identity.empty()) {
    return false;
  }
  if (backend == DIGITOR_RENDERER_VULKAN) {
    return value.vulkan_external_memory && value.vulkan_external_semaphore;
  }
  return backend == DIGITOR_RENDERER_D3D12;
}

[[nodiscard]] NativeImplementationEvidence evidence(
    const std::string& identity) {
  NativeImplementationEvidence out{};
  out.production_implementation = true;
  out.native_api_bound = true;
  out.synchronization_bound = true;
  out.zero_copy_telemetry_bound = true;
  out.implementation_identity = identity;
  return out;
}

}  // namespace

WindowsNativeProviderResult create_windows_native_provider(
    WindowsNativeProviderBindings bindings) noexcept {
  WindowsNativeProviderResult out{};
  const auto backend = bindings.timeline.backend;

#if !defined(_WIN32)
  out.result = DIGITOR_RESULT_UNSUPPORTED;
  out.diagnostic = "Windows native provider can only be created on Windows";
  return out;
#else
  if (backend != DIGITOR_RENDERER_D3D12 &&
      backend != DIGITOR_RENDERER_VULKAN) {
    out.result = DIGITOR_RESULT_UNSUPPORTED;
    out.diagnostic = "Windows provider requires D3D12 or Vulkan";
    return out;
  }
  if (!bindings.device_identity ||
      bindings.timeline.context_identity != bindings.device_identity) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "timeline and Windows device identity must match";
    return out;
  }
  if (!capabilities_valid(bindings.capabilities, backend)) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "required Windows native APIs or zero-copy capabilities are unavailable";
    return out;
  }
  if (!bindings.flutter.flutter_texture_registrar ||
      bindings.flutter.implementation_identity.empty() ||
      !bindings.flutter.attached) {
    out.result = DIGITOR_RESULT_NOT_INITIALIZED;
    out.diagnostic = "real Flutter Windows texture bridge is required";
    return out;
  }
  if (bindings.package_identity.empty() || bindings.build_identity.empty()) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "Windows provider package/build identity is required";
    return out;
  }
  if (!bindings.timeline.create_target || !bindings.timeline.execute_effects ||
      !bindings.timeline.composite_layer || !bindings.timeline.frame_evictable) {
    out.result = DIGITOR_RESULT_NOT_INITIALIZED;
    out.diagnostic = "Windows native timeline bindings are incomplete";
    return out;
  }
  if (!bindings.encoder.open || !bindings.encoder.submit ||
      !bindings.encoder.drain || !bindings.encoder.finalize_atomic ||
      !bindings.encoder.cancel || !bindings.encoder.qualification) {
    out.result = DIGITOR_RESULT_NOT_INITIALIZED;
    out.diagnostic = "Windows native hardware encoder bindings are incomplete";
    return out;
  }
  if (backend == DIGITOR_RENDERER_VULKAN &&
      !validate_windows_vulkan_zero_copy_interop(bindings.vulkan_interop)) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "Windows Vulkan/DXGI external-memory interop is incomplete";
    return out;
  }

  NativePlatformProvider provider{};
  provider.platform = ProductionPlatform::windows;
  provider.timeline = evidence("windows.timeline." + bindings.capabilities.adapter_identity);
  provider.flutter_texture =
      evidence("windows.flutter." + bindings.flutter.implementation_identity);
  provider.encoder = evidence("windows.encoder." + bindings.capabilities.encoder_identity);
  provider.package_identity = std::move(bindings.package_identity);
  provider.build_identity = std::move(bindings.build_identity);

  provider.create = [bindings = std::move(bindings)](
                        ProductionPlatformFactoryInputs inputs) mutable {
    inputs.platform = ProductionPlatform::windows;
    inputs.timeline = bindings.timeline;
    inputs.flutter.platform = ProductionPlatform::windows;
    inputs.flutter.backend = bindings.timeline.backend;
    inputs.flutter.device_identity = bindings.device_identity;
    inputs.flutter.device_name = bindings.capabilities.adapter_identity;
    inputs.flutter.attached = bindings.flutter.attached;
    inputs.flutter.delivery_mode =
        FlutterPreviewDeliveryMode::deferred_to_flutter_texture;
    inputs.encoder.windows = bindings.encoder;
    inputs.windows_vulkan = bindings.vulkan_interop;
    return create_production_platform_assembly(std::move(inputs));
  };

  const auto validation = validate_native_platform_provider(provider);
  if (!validation) {
    out.result = validation.result;
    out.diagnostic = validation.diagnostic;
    return out;
  }
  out.provider = std::move(provider);
  out.result = DIGITOR_RESULT_OK;
  return out;
#endif
}

}  // namespace digitor

#include "windows_d3d12_effect_provider.cpp"
#include "windows_d3d12_builtin_effect_shaders.cpp"
