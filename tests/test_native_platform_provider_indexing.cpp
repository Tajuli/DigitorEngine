#include "digitor/native_platform_provider_fixed.hpp"

#include <cassert>
#include <string>

using namespace digitor;

namespace {
NativeImplementationEvidence evidence(std::string id) {
  NativeImplementationEvidence value{};
  value.production_implementation = true;
  value.native_api_bound = true;
  value.synchronization_bound = true;
  value.zero_copy_telemetry_bound = true;
  value.implementation_identity = std::move(id);
  return value;
}

NativePlatformProvider provider(ProductionPlatform platform, const char* prefix) {
  NativePlatformProvider value{};
  value.platform = platform;
  value.timeline = evidence(std::string(prefix) + "-timeline");
  value.flutter_texture = evidence(std::string(prefix) + "-flutter");
  value.encoder = evidence(std::string(prefix) + "-encoder");
  value.package_identity = std::string(prefix) + "-package";
  value.build_identity = std::string(prefix) + "-build";
  value.create = [](ProductionPlatformFactoryInputs inputs) {
    return create_production_platform_assembly(std::move(inputs));
  };
  return value;
}
}

int main() {
  static_assert(*native_platform_slot(ProductionPlatform::windows) == 0);
  static_assert(*native_platform_slot(ProductionPlatform::android) == 1);
  static_assert(*native_platform_slot(ProductionPlatform::macos) == 2);
  static_assert(*native_platform_slot(ProductionPlatform::ios) == 3);
  static_assert(*native_provider_index(ProductionPlatform::ios) == 3);

  NativePlatformProviderRegistry registry;
  assert(registry.install(provider(ProductionPlatform::windows, "windows")) == DIGITOR_RESULT_OK);
  assert(registry.install(provider(ProductionPlatform::android, "android")) == DIGITOR_RESULT_OK);
  assert(registry.install(provider(ProductionPlatform::macos, "macos")) == DIGITOR_RESULT_OK);
  assert(!registry.complete());
  assert(registry.install(provider(ProductionPlatform::ios, "ios")) == DIGITOR_RESULT_OK);
  assert(registry.complete());
  assert(registry.provider(ProductionPlatform::windows));
  assert(registry.provider(ProductionPlatform::android));
  assert(registry.provider(ProductionPlatform::macos));
  assert(registry.provider(ProductionPlatform::ios));
  assert(registry.install(provider(ProductionPlatform::ios, "ios-duplicate")) ==
         DIGITOR_RESULT_RESOURCE_IN_USE);

  auto flutter_encoder_collision = provider(ProductionPlatform::windows, "bad-a");
  flutter_encoder_collision.encoder.implementation_identity =
      flutter_encoder_collision.flutter_texture.implementation_identity;
  assert(!validate_native_platform_provider(flutter_encoder_collision));

  auto timeline_flutter_collision = provider(ProductionPlatform::android, "bad-b");
  timeline_flutter_collision.flutter_texture.implementation_identity =
      timeline_flutter_collision.timeline.implementation_identity;
  assert(!validate_native_platform_provider_strict(timeline_flutter_collision));

  auto timeline_encoder_collision = provider(ProductionPlatform::macos, "bad-c");
  timeline_encoder_collision.encoder.implementation_identity =
      timeline_encoder_collision.timeline.implementation_identity;
  assert(!validate_native_platform_provider(timeline_encoder_collision));

  const auto ios = provider(ProductionPlatform::ios, "ios-readiness");
  const auto readiness = strict_source_readiness_from_provider(ios, true, true, true);
  assert(readiness.platform == SourceReleasePlatform::ios);
  return 0;
}
