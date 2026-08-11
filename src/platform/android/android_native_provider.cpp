#include "digitor/android_native_provider.hpp"

#include <utility>

namespace digitor {
namespace {

[[nodiscard]] bool capabilities_valid(
    const AndroidNativeProviderCapabilities& value,
    DigitorRendererBackend backend) noexcept {
  if (!value.android_ndk || !value.api_26_or_newer ||
      !value.mediacodec_ndk || !value.native_window ||
      !value.ahardwarebuffer || !value.flutter_texture_bridge ||
      !value.hardware_encoder || !value.input_surface ||
      !value.zero_cpu_readback || !value.zero_cpu_staging ||
      value.device_identity.empty() || value.codec_identity.empty()) {
    return false;
  }
  if (backend == DIGITOR_RENDERER_VULKAN) {
    return value.vulkan_external_memory_ahb &&
           value.vulkan_external_semaphore_fd;
  }
  if (backend == DIGITOR_RENDERER_OPENGL_ES) {
    return value.gles_eglimage && value.gles_native_fence_sync;
  }
  return false;
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

AndroidNativeProviderResult create_android_native_provider(
    AndroidNativeProviderBindings bindings) noexcept {
  AndroidNativeProviderResult out{};
  const auto backend = bindings.timeline.backend;

#if !defined(__ANDROID__)
  out.result = DIGITOR_RESULT_UNSUPPORTED;
  out.diagnostic = "Android native provider can only be created on Android";
  return out;
#else
  if (backend != DIGITOR_RENDERER_VULKAN &&
      backend != DIGITOR_RENDERER_OPENGL_ES) {
    out.result = DIGITOR_RESULT_UNSUPPORTED;
    out.diagnostic = "Android provider requires Vulkan or OpenGL ES";
    return out;
  }
  if (!bindings.device_identity ||
      bindings.timeline.context_identity != bindings.device_identity) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "timeline and Android device identity must match";
    return out;
  }
  if (!capabilities_valid(bindings.capabilities, backend)) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "required Android native APIs or zero-copy capabilities are unavailable";
    return out;
  }
  if (!bindings.flutter.flutter_texture_registrar ||
      bindings.flutter.implementation_identity.empty() ||
      !bindings.flutter.attached) {
    out.result = DIGITOR_RESULT_NOT_INITIALIZED;
    out.diagnostic = "real Flutter Android texture bridge is required";
    return out;
  }
  if (bindings.package_identity.empty() || bindings.build_identity.empty()) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "Android provider package/build identity is required";
    return out;
  }
  if (!bindings.timeline.create_target || !bindings.timeline.execute_effects ||
      !bindings.timeline.composite_layer || !bindings.timeline.frame_evictable) {
    out.result = DIGITOR_RESULT_NOT_INITIALIZED;
    out.diagnostic = "Android native timeline bindings are incomplete";
    return out;
  }
  if (!bindings.encoder.open || !bindings.encoder.submit ||
      !bindings.encoder.drain || !bindings.encoder.finalize_mp4_atomic ||
      !bindings.encoder.cancel || !bindings.encoder.qualification) {
    out.result = DIGITOR_RESULT_NOT_INITIALIZED;
    out.diagnostic = "Android MediaCodec encoder bindings are incomplete";
    return out;
  }

  NativePlatformProvider provider{};
  provider.platform = ProductionPlatform::android;
  provider.timeline = evidence("android.timeline." + bindings.capabilities.device_identity);
  provider.flutter_texture =
      evidence("android.flutter." + bindings.flutter.implementation_identity);
  provider.encoder = evidence("android.encoder." + bindings.capabilities.codec_identity);
  provider.package_identity = std::move(bindings.package_identity);
  provider.build_identity = std::move(bindings.build_identity);

  provider.create = [bindings = std::move(bindings)](
                        ProductionPlatformFactoryInputs inputs) mutable {
    inputs.platform = ProductionPlatform::android;
    inputs.timeline = bindings.timeline;
    inputs.flutter.platform = ProductionPlatform::android;
    inputs.flutter.backend = bindings.timeline.backend;
    inputs.flutter.device_identity = bindings.device_identity;
    inputs.flutter.device_name = bindings.capabilities.device_identity;
    inputs.flutter.attached = bindings.flutter.attached;
    inputs.flutter.delivery_mode =
        FlutterPreviewDeliveryMode::deferred_to_flutter_texture;
    inputs.encoder.android = bindings.encoder;
    return create_production_platform_assembly(std::move(inputs));
  };

  const auto validation = validate_native_platform_provider_strict(provider);
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
