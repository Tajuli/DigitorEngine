#include "digitor/apple_native_provider.hpp"

#include <cassert>
#include <utility>

int main() {
  using namespace digitor;

  AppleNativeProviderBindings macos{};
  macos.platform = ProductionPlatform::macos;
  macos.capabilities.platform = ApplePlatform::macos;
  auto mac_result = create_apple_native_provider(std::move(macos));

  AppleNativeProviderBindings ios{};
  ios.platform = ProductionPlatform::ios;
  ios.capabilities.platform = ApplePlatform::ios;
  auto ios_result = create_apple_native_provider(std::move(ios));

#if defined(__APPLE__)
  assert(!mac_result);
  assert(!ios_result);
  assert(mac_result.result != DIGITOR_RESULT_OK);
  assert(ios_result.result != DIGITOR_RESULT_OK);
#else
  assert(!mac_result);
  assert(!ios_result);
  assert(mac_result.result == DIGITOR_RESULT_UNSUPPORTED);
  assert(ios_result.result == DIGITOR_RESULT_UNSUPPORTED);
#endif

  NativePlatformProvider invalid{};
  invalid.platform = ProductionPlatform::macos;
  assert(!validate_native_platform_provider_strict(invalid));
  return 0;
}
