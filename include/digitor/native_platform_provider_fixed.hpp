#pragma once

#include "digitor/native_platform_provider.hpp"

namespace digitor {

[[nodiscard]] constexpr std::optional<std::size_t> native_provider_index(
    ProductionPlatform platform) noexcept {
  return native_platform_slot(platform);
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
  return validate_native_platform_provider(provider);
}

using StrictNativePlatformProviderRegistry = NativePlatformProviderRegistry;

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
