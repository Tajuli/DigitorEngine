#include "digitor/native_platform_provider_fixed.hpp"

#include <cassert>

using namespace digitor;

namespace {
NativeImplementationEvidence evidence(const char* id) {
  NativeImplementationEvidence value{};
  value.production_implementation = true;
  value.native_api_bound = true;
  value.synchronization_bound = true;
  value.zero_copy_telemetry_bound = true;
  value.implementation_identity = id;
  return value;
}

NativePlatformProvider provider(ProductionPlatform platform, const char* prefix) {
  NativePlatformProvider value{};
  value.platform = platform;
  value.timeline = evidence((std::string(prefix) + "-timeline").c_str());
  value.flutter_texture = evidence((std::string(prefix) + "-flutter").c_str());
  value.encoder = evidence((std::string(prefix) + "-encoder").c_str());
  value.package_identity = std::string(prefix) + "-package";
  value.build_identity = std::string(prefix) + "-build";
  value.create = [](ProductionPlatformFactoryInputs inputs) {
    return create_production_platform_assembly(std::move(inputs));
  };
  return value;
}
}

int main() {
  static_assert(*native_provider_index(ProductionPlatform::windows) == 0);
  static_assert(*native_provider_index(ProductionPlatform::android) == 1);
  static_assert(*native_provider_index(ProductionPlatform::macos) == 2);
  static_assert(*native_provider_index(ProductionPlatform::ios) == 3);

  StrictNativePlatformProviderRegistry registry;
  assert(registry.install(provider(ProductionPlatform::windows, "windows")) == DIGITOR_RESULT_OK);
  assert(registry.install(provider(ProductionPlatform::android, "android")) == DIGITOR_RESULT_OK);
  assert(registry.install(provider(ProductionPlatform::macos, "macos")) == DIGITOR_RESULT_OK);
  assert(!registry.complete());
  assert(registry.install(provider(ProductionPlatform::ios, "ios")) == DIGITOR_RESULT_OK);
  assert(registry.complete());
  assert(registry.provider(ProductionPlatform::ios));

  auto duplicate_identity = provider(ProductionPlatform::windows, "bad");
  duplicate_identity.encoder.implementation_identity =
      duplicate_identity.flutter_texture.implementation_identity;
  assert(!validate_native_platform_provider_strict(duplicate_identity));

  const auto ios = provider(ProductionPlatform::ios, "ios-readiness");
  const auto readiness = strict_source_readiness_from_provider(ios, true, true, true);
  assert(readiness.platform == SourceReleasePlatform::ios);
  return 0;
}
