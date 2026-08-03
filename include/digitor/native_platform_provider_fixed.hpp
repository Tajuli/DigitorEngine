#pragma once

#include "digitor/native_platform_provider.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace digitor {

[[nodiscard]] constexpr std::optional<std::size_t> native_provider_index(
    ProductionPlatform platform) noexcept {
  switch (platform) {
    case ProductionPlatform::windows: return 0;
    case ProductionPlatform::android: return 1;
    case ProductionPlatform::macos: return 2;
    case ProductionPlatform::ios: return 3;
  }
  return std::nullopt;
}

[[nodiscard]] constexpr SourceReleasePlatform source_release_platform(
    ProductionPlatform platform) noexcept {
  switch (platform) {
    case ProductionPlatform::windows: return SourceReleasePlatform::windows;
    case ProductionPlatform::android: return SourceReleasePlatform::android;
    case ProductionPlatform::macos: return SourceReleasePlatform::macos;
    case ProductionPlatform::ios: return SourceReleasePlatform::ios;
  }
  return SourceReleasePlatform::windows;
}

[[nodiscard]] inline NativeProviderValidation validate_native_platform_provider_strict(
    const NativePlatformProvider& provider) {
  const auto base = validate_native_platform_provider(provider);
  if (!base) return base;
  if (provider.flutter_texture.implementation_identity ==
      provider.encoder.implementation_identity) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT,
            "timeline, Flutter texture and encoder identities must all be distinct"};
  }
  if (!native_provider_index(provider.platform)) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "invalid provider platform"};
  }
  return {DIGITOR_RESULT_OK, "native provider valid"};
}

class StrictNativePlatformProviderRegistry final {
 public:
  DigitorResult install(NativePlatformProvider provider,
                        std::string* diagnostic = nullptr) {
    const auto validation = validate_native_platform_provider_strict(provider);
    if (!validation) {
      if (diagnostic) *diagnostic = validation.diagnostic;
      return validation.result;
    }
    const auto index = native_provider_index(provider.platform);
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
    const auto index = native_provider_index(platform);
    return index ? providers_[*index] : nullptr;
  }

  [[nodiscard]] bool complete() const noexcept {
    for (const auto& provider : providers_) {
      if (!provider || !validate_native_platform_provider_strict(*provider)) return false;
    }
    return true;
  }

 private:
  std::array<std::shared_ptr<NativePlatformProvider>, 4> providers_{};
};

[[nodiscard]] inline PlatformSourceReadiness strict_source_readiness_from_provider(
    const NativePlatformProvider& provider, bool platform_compile_passed,
    bool backend_matches_snapshot, bool device_identity_matches) {
  auto out = source_readiness_from_provider(provider, platform_compile_passed,
                                            backend_matches_snapshot,
                                            device_identity_matches);
  out.platform = source_release_platform(provider.platform);
  return out;
}

}  // namespace digitor
