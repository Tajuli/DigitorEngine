#include "digitor/android_native_provider.hpp"

#include <cassert>
#include <utility>

int main() {
  using namespace digitor;

  AndroidNativeProviderBindings bindings{};
  auto result = create_android_native_provider(std::move(bindings));
#if defined(__ANDROID__)
  assert(!result);
  assert(result.result != DIGITOR_RESULT_OK);
#else
  assert(!result);
  assert(result.result == DIGITOR_RESULT_UNSUPPORTED);
#endif

  NativePlatformProvider invalid{};
  invalid.platform = ProductionPlatform::android;
  assert(!validate_native_platform_provider_strict(invalid));
  return 0;
}
