#include "digitor/apple_native_provider.hpp"

#include <cassert>
#include <utility>

int main() {
  using namespace digitor;

  AppleNativeProviderBindings macos{};
  macos.platform = ProductionPlatform::macos;
  auto mac_result = create_apple_native_provider(std::move(macos));
#if defined(__APPLE__)
  assert(!mac_result);
  assert(mac_result.result != DIGITOR_RESULT_OK);
#else
  assert(!mac_result);
  assert(mac_result.result == DIGITOR_RESULT_UNSUPPORTED);
#endif

  AppleNativeProviderBindings ios{};
  ios.platform = ProductionPlatform::ios;
  auto ios_result = create_apple_native_provider(std::move(ios));
  assert(!ios_result);

  NativePlatformProvider invalid{};
  invalid.platform = ProductionPlatform::macos;
  assert(!validate_native_platform_provider_strict(invalid));
  invalid.platform = ProductionPlatform::ios;
  assert(!validate_native_platform_provider_strict(invalid));
  return 0;
}
