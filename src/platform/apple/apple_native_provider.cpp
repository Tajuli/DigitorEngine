#include "digitor/apple_native_provider.hpp"

#include <utility>

namespace digitor {
namespace {
NativeImplementationEvidence evidence(const std::string& id) {
  NativeImplementationEvidence out{};
  out.production_implementation = true;
  out.native_api_bound = true;
  out.synchronization_bound = true;
  out.zero_copy_telemetry_bound = true;
  out.implementation_identity = id;
  return out;
}

bool capabilities_valid(const AppleNativeProviderCapabilities& c) noexcept {
  return c.metal && c.iosurface && c.cvpixelbuffer && c.video_toolbox &&
         c.hardware_encoder && c.flutter_texture_bridge && c.lifecycle_safe &&
         c.zero_cpu_readback && c.zero_cpu_staging &&
         !c.device_identity.empty() && !c.encoder_identity.empty();
}
}  // namespace

AppleNativeProviderResult create_apple_native_provider(
    AppleNativeProviderBindings bindings) noexcept {
  AppleNativeProviderResult out{};
#if !defined(__APPLE__)
  out.result = DIGITOR_RESULT_UNSUPPORTED;
  out.diagnostic = "Apple native provider can only be created on Apple platforms";
  return out;
#else
  if (bindings.timeline.backend != DIGITOR_RENDERER_METAL) {
    out.result = DIGITOR_RESULT_UNSUPPORTED;
    out.diagnostic = "Apple provider requires Metal";
    return out;
  }
  if (!bindings.device_identity ||
      bindings.timeline.context_identity != bindings.device_identity) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "timeline and Apple device identity must match";
    return out;
  }
  if (!capabilities_valid(bindings.capabilities)) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "required Metal/IOSurface/VideoToolbox capabilities are unavailable";
    return out;
  }
  if (!bindings.flutter.texture_registrar ||
      bindings.flutter.implementation_identity.empty() ||
      !bindings.flutter.attached || !bindings.flutter.lifecycle_ready ||
      !bindings.flutter.present || !bindings.flutter.attached() ||
      !bindings.flutter.lifecycle_ready()) {
    out.result = DIGITOR_RESULT_NOT_INITIALIZED;
    out.diagnostic = "real lifecycle-ready Flutter Apple texture bridge is required";
    return out;
  }
  if (!bindings.timeline.create_target || !bindings.timeline.execute_effects ||
      !bindings.timeline.composite_layer || !bindings.timeline.frame_evictable ||
      !bindings.encoder.open || !bindings.encoder.submit ||
      !bindings.encoder.drain || !bindings.encoder.finalize_atomic ||
      !bindings.encoder.cancel || !bindings.encoder.qualification) {
    out.result = DIGITOR_RESULT_NOT_INITIALIZED;
    out.diagnostic = "Apple timeline or VideoToolbox bindings are incomplete";
    return out;
  }
  if (bindings.package_identity.empty() || bindings.build_identity.empty()) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "Apple package/build identity is required";
    return out;
  }

  const bool ios = bindings.capabilities.platform == ApplePlatform::ios;
  NativePlatformProvider provider{};
  provider.platform = ios ? ProductionPlatform::ios : ProductionPlatform::macos;
  provider.timeline = evidence(std::string(ios ? "ios" : "macos") +
                               ".timeline." + bindings.capabilities.device_identity);
  provider.flutter_texture = evidence(std::string(ios ? "ios" : "macos") +
                                      ".flutter." + bindings.flutter.implementation_identity);
  provider.encoder = evidence(std::string(ios ? "ios" : "macos") +
                              ".encoder." + bindings.capabilities.encoder_identity);
  provider.package_identity = std::move(bindings.package_identity);
  provider.build_identity = std::move(bindings.build_identity);
  provider.create = [bindings = std::move(bindings), ios](
                        ProductionPlatformFactoryInputs inputs) mutable {
    inputs.platform = ios ? ProductionPlatform::ios : ProductionPlatform::macos;
    inputs.timeline = bindings.timeline;
    inputs.flutter.platform = inputs.platform;
    inputs.flutter.backend = DIGITOR_RENDERER_METAL;
    inputs.flutter.device_identity = bindings.device_identity;
    inputs.flutter.device_name = bindings.capabilities.device_identity;
    inputs.flutter.attached = [bridge = bindings.flutter]() mutable {
      return bridge.attached && bridge.lifecycle_ready &&
             bridge.attached() && bridge.lifecycle_ready();
    };
    inputs.flutter.register_or_present = bindings.flutter.present;
    inputs.encoder.apple = bindings.encoder;
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
