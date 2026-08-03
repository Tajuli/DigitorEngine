#pragma once

#include "digitor/production_platform_integration.hpp"
#include "digitor/source_release_readiness.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace digitor {

struct NativePlatformProvider final {
  ProductionPlatform platform{ProductionPlatform::windows};
  NativeImplementationEvidence timeline;
  NativeImplementationEvidence flutter_texture;
  NativeImplementationEvidence encoder;
  std::string package_identity;
  std::string build_identity;
  std::function<ProductionPlatformAssembly(ProductionPlatformFactoryInputs)> create;
};

struct NativeProviderValidation final {
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] constexpr std::optional<std::size_t> native_platform_slot(
    ProductionPlatform platform) noexcept {
  switch (platform) {
    case ProductionPlatform::windows: return 0;
    case ProductionPlatform::android: return 1;
    case ProductionPlatform::macos: return 2;
    case ProductionPlatform::ios: return 3;
  }
  return std::nullopt;
}

[[nodiscard]] inline NativeProviderValidation validate_native_platform_provider(
    const NativePlatformProvider& provider) {
  if (!provider.timeline.valid() || !provider.flutter_texture.valid() ||
      !provider.encoder.valid()) {
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
            "timeline, Flutter texture and encoder must be production-native"};
  }
  if (provider.package_identity.empty() || provider.build_identity.empty() ||
      !provider.create) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "provider package/build identity and factory are required"};
  }
  if (!native_platform_slot(provider.platform)) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "invalid provider platform"};
  }
  const auto& timeline = provider.timeline.implementation_identity;
  const auto& flutter = provider.flutter_texture.implementation_identity;
  const auto& encoder = provider.encoder.implementation_identity;
  if (timeline == flutter || timeline == encoder || flutter == encoder) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "native component identities must identify distinct implementations"};
  }
  return {DIGITOR_RESULT_OK, "native provider valid"};
}

class NativePlatformProviderRegistry final {
 public:
  DigitorResult install(NativePlatformProvider provider,
                        std::string* diagnostic = nullptr) {
    const auto validation = validate_native_platform_provider(provider);
    if (!validation) {
      if (diagnostic) *diagnostic = validation.diagnostic;
      return validation.result;
    }
    const auto index = native_platform_slot(provider.platform);
    if (!index) {
      if (diagnostic) *diagnostic = "invalid platform provider index";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    if (providers_[*index]) {
      if (diagnostic) *diagnostic = "platform provider already installed";
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    providers_[*index] = std::make_shared<NativePlatformProvider>(std::move(provider));
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  }

  [[nodiscard]] std::shared_ptr<const NativePlatformProvider> provider(
      ProductionPlatform platform) const noexcept {
    const auto index = native_platform_slot(platform);
    return index ? providers_[*index] : nullptr;
  }

  [[nodiscard]] bool complete() const noexcept {
    for (const auto& provider : providers_) {
      if (!provider || !validate_native_platform_provider(*provider)) return false;
    }
    return true;
  }

 private:
  std::array<std::shared_ptr<NativePlatformProvider>, 4> providers_{};
};

[[nodiscard]] inline PlatformSourceReadiness source_readiness_from_provider(
    const NativePlatformProvider& provider, bool platform_compile_passed,
    bool backend_matches_snapshot, bool device_identity_matches) {
  PlatformSourceReadiness out{};
  switch (provider.platform) {
    case ProductionPlatform::windows: out.platform = SourceReleasePlatform::windows; break;
    case ProductionPlatform::android: out.platform = SourceReleasePlatform::android; break;
    case ProductionPlatform::macos: out.platform = SourceReleasePlatform::macos; break;
    case ProductionPlatform::ios: out.platform = SourceReleasePlatform::ios; break;
  }
  out.timeline_binding = provider.timeline.valid()
                             ? NativeBindingKind::production_native
                             : NativeBindingKind::callback_contract;
  out.flutter_texture_binding = provider.flutter_texture.valid()
                                    ? NativeBindingKind::production_native
                                    : NativeBindingKind::callback_contract;
  out.encoder_binding = provider.encoder.valid()
                            ? NativeBindingKind::production_native
                            : NativeBindingKind::callback_contract;
  out.selected_backend_matches_snapshot = backend_matches_snapshot;
  out.selected_device_identity_matches = device_identity_matches;
  out.native_synchronization_bound = provider.timeline.synchronization_bound &&
                                     provider.flutter_texture.synchronization_bound &&
                                     provider.encoder.synchronization_bound;
  out.zero_copy_telemetry_bound = provider.timeline.zero_copy_telemetry_bound &&
                                  provider.flutter_texture.zero_copy_telemetry_bound &&
                                  provider.encoder.zero_copy_telemetry_bound;
  out.platform_compile_passed = platform_compile_passed;
  out.implementation_identity = provider.package_identity + ":" + provider.build_identity;
  return out;
}

}  // namespace digitor
