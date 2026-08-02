#include "digitor/native_platform_provider.hpp"

#include <cassert>
#include <string>

using namespace digitor;

namespace {
NativeImplementationEvidence evidence(const char* identity) {
  NativeImplementationEvidence value{};
  value.production_implementation = true;
  value.native_api_bound = true;
  value.synchronization_bound = true;
  value.zero_copy_telemetry_bound = true;
  value.implementation_identity = identity;
  return value;
}

NativePlatformProvider provider(ProductionPlatform platform, const char* package) {
  NativePlatformProvider value{};
  value.platform = platform;
  value.timeline = evidence((std::string(package) + ":timeline").c_str());
  value.flutter_texture = evidence((std::string(package) + ":flutter").c_str());
  value.encoder = evidence((std::string(package) + ":encoder").c_str());
  value.package_identity = package;
  value.build_identity = "test-build";
  value.create = [](ProductionPlatformFactoryInputs inputs) {
    return create_production_platform_assembly(std::move(inputs));
  };
  return value;
}
}  // namespace

int main() {
  NativePlatformProviderRegistry registry;
  std::string diagnostic;

  assert(registry.install(provider(ProductionPlatform::windows, "windows-native"),
                          &diagnostic) == DIGITOR_RESULT_OK);
  assert(registry.install(provider(ProductionPlatform::android, "android-native"),
                          &diagnostic) == DIGITOR_RESULT_OK);
  assert(registry.install(provider(ProductionPlatform::macos, "macos-native"),
                          &diagnostic) == DIGITOR_RESULT_OK);
  assert(!registry.complete());
  assert(registry.install(provider(ProductionPlatform::ios, "ios-native"),
                          &diagnostic) == DIGITOR_RESULT_OK);
  assert(registry.complete());

  auto invalid = provider(ProductionPlatform::windows, "invalid");
  invalid.encoder.production_implementation = false;
  assert(!validate_native_platform_provider(invalid));

  const auto windows = registry.provider(ProductionPlatform::windows);
  assert(windows);
  const auto readiness = source_readiness_from_provider(
      *windows, true, true, true);
  assert(readiness.timeline_binding == NativeBindingKind::production_native);
  assert(readiness.flutter_texture_binding == NativeBindingKind::production_native);
  assert(readiness.encoder_binding == NativeBindingKind::production_native);
  assert(readiness.native_synchronization_bound);
  assert(readiness.zero_copy_telemetry_bound);
  assert(readiness.platform_compile_passed);
  return 0;
}
