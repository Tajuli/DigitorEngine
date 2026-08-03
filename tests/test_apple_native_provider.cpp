#include "digitor/apple_native_provider.hpp"

#include <cassert>

int main() {
  using namespace digitor;
  AppleNativeProviderBindings bindings{};
  auto result = create_apple_native_provider(std::move(bindings));
#if defined(__APPLE__)
  assert(!result);
  assert(result.result != DIGITOR_RESULT_OK);
#else
  assert(!result);
  assert(result.result == DIGITOR_RESULT_UNSUPPORTED);
#endif
  return 0;
}
