#include "digitor/apple_native_provider.hpp"

#include <cassert>
#include <utility>

int main() {
  using namespace digitor;

  AppleNativeProviderBindings bindings{};
  bindings.platform = ProductionPlatform::macos;
  auto result = create_apple_native_provider(std::move(bindings));
#if defined(__APPLE__)
  assert(!result);
  assert(result.result != DIGITOR_RESULT_OK);
#else
  assert(!result);
  assert(result.result == DIGITOR_RESULT_UNSUPPORTED);
#endif

  NativePlatformProvider invalid{};
  invalid.platform = ProductionPlatform::ios;
  assert(!validate_native_platform_provider_strict(invalid));
  return 0;
}
