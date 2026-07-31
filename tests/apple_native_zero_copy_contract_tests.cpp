#include "digitor/apple_native_zero_copy.hpp"

#include <cassert>
#include <iostream>

int main() {
  using namespace digitor;
  AppleNativeZeroCopyConfig config{};
  AppleNativeZeroCopyBindings bindings(
      config,
      [](const AppleMetalImportedFrame&, void*, ProcessedGpuFramePtr&) {
        return DIGITOR_RESULT_UNSUPPORTED;
      },
      [](const ProcessedGpuFramePtr&, void*&) {
        return DIGITOR_RESULT_UNSUPPORTED;
      });

#if defined(__APPLE__)
  // Missing application-owned Metal/VideoToolbox objects must fail closed.
  assert(bindings.initialize() == DIGITOR_RESULT_INVALID_ARGUMENT);
#else
  assert(bindings.initialize() == DIGITOR_RESULT_UNSUPPORTED);
  ApplePixelBufferFrame decoded{};
  AppleMetalImportedFrame imported{};
  assert(bindings.import_metal(decoded, imported) == DIGITOR_RESULT_UNSUPPORTED);
  ProcessedGpuFramePtr frame;
  assert(bindings.submit_encoder(frame) == DIGITOR_RESULT_UNSUPPORTED);
#endif

  const auto telemetry = bindings.telemetry();
  assert(telemetry.cpu_copies == 0);
  assert(bindings.gpu_only());
  std::cout << "APPLE_NATIVE_ZERO_COPY_CONTRACT status=PASS cpu_copies="
            << telemetry.cpu_copies << "\n";
  return 0;
}
