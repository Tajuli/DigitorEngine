#include "digitor/apple_zero_copy_pipeline.hpp"
#include <cassert>

int main() {
  using namespace digitor;
  AppleZeroCopyConfig config{};
  AppleZeroCopyBinding empty{};
  AppleZeroCopyPipeline invalid(config, empty);
  assert(invalid.initialize() == DIGITOR_RESULT_INVALID_ARGUMENT);
  const auto telemetry = invalid.telemetry();
  assert(telemetry.cpu_copies == 0);
  assert(telemetry.cpu_fallback_frames == 0);
  return 0;
}
