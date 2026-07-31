#include "digitor/apple_metal_zero_copy_runtime.hpp"
#include <cassert>
int main(){
  using namespace digitor;
  AppleMetalRuntimeConfig invalid{};
  AppleMetalZeroCopyRuntime runtime(invalid);
#if defined(__APPLE__)
  assert(runtime.initialize()==DIGITOR_RESULT_INVALID_ARGUMENT);
#else
  assert(runtime.initialize()==DIGITOR_RESULT_UNSUPPORTED);
#endif
  const auto t=runtime.telemetry();
  assert(t.cpu_copies==0);
  assert(t.cpu_fallbacks==0);
  assert(runtime.gpu_only());
  auto preview=runtime.preview_consumer();
  assert(preview({})==DIGITOR_RESULT_INVALID_ARGUMENT);
  auto encoder=runtime.encoder_consumer();
  assert(encoder({})==DIGITOR_RESULT_INVALID_ARGUMENT);
  return 0;
}