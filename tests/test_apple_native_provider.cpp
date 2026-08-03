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

  AppleNativeProviderBindings ios{};
  ios.platform = ProductionPlatform::ios;
  auto ios_result = create_apple_native_provider(std::move(ios));
#if defined(__APPLE__)
  assert(!ios_result);
#else
  assert(ios_result.result == DIGITOR_RESULT_UNSUPPORTED);
#endif
  return 0;
}
