#include "digitor/apple_native_provider.hpp"

#include <cassert>
#include <utility>

int main() {
  using namespace digitor;

  AppleNativeProviderBindings macos{};
  macos.platform = ProductionPlatform::macos;
  auto macos_result = create_apple_native_provider(std::move(macos));
#if defined(__APPLE__)
  assert(!macos_result);
  assert(macos_result.result != DIGITOR_RESULT_OK);
#else
  assert(!macos_result);
  assert(macos_result.result == DIGITOR_RESULT_UNSUPPORTED);
#endif

  NativePlatformProvider invalid{};
  invalid.platform = ProductionPlatform::ios;
  assert(!validate_native_platform_provider_strict(invalid));
  return 0;
}
