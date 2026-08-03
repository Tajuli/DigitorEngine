#include "digitor/windows_native_provider.hpp"

#include <cassert>

int main() {
  using namespace digitor;

  WindowsNativeProviderBindings bindings{};
  auto result = create_windows_native_provider(std::move(bindings));
#if defined(_WIN32)
  assert(!result);
  assert(result.result != DIGITOR_RESULT_OK);
#else
  assert(!result);
  assert(result.result == DIGITOR_RESULT_UNSUPPORTED);
#endif
  return 0;
}
