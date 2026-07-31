#include "digitor/apple_metal_p010.hpp"
#include <cassert>

using namespace digitor;

int main() {
  AppleMetalP010Config bad{};
  AppleMetalP010NativeContext native{};
  AppleMetalP010Pipeline invalid(bad, native);
  assert(invalid.initialize() == DIGITOR_RESULT_INVALID_ARGUMENT);

  bad.width = 1921;
  bad.height = 1080;
  AppleMetalP010Pipeline odd(bad, native);
  assert(odd.initialize() == DIGITOR_RESULT_INVALID_ARGUMENT);

  const auto telemetry = odd.telemetry();
  assert(telemetry.cpu_copies == 0);
  assert(telemetry.cpu_fallback_frames == 0);
  assert(!odd.production_active());
  return 0;
}
