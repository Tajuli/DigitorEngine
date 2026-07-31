#include "digitor/android_native_zero_copy.hpp"

#include <cassert>

int main() {
  using namespace digitor;

  AndroidNativeInteropConfig config{};
  AndroidNativeZeroCopyBindings bindings(
      config,
      {},
      {},
      [](const ProcessedGpuFramePtr&) { return DIGITOR_RESULT_OK; });

#if defined(__ANDROID__)
  assert(bindings.initialize() == DIGITOR_RESULT_INVALID_ARGUMENT ||
         bindings.initialize() == DIGITOR_RESULT_UNSUPPORTED);
#else
  assert(bindings.initialize() == DIGITOR_RESULT_UNSUPPORTED);
#endif

  AndroidHardwareBufferFrame input{};
  AndroidImportedImage imported{};
  assert(bindings.import_vulkan(input, imported) != DIGITOR_RESULT_OK);
  assert(bindings.import_gles(input, imported) != DIGITOR_RESULT_OK);

  ProcessedGpuFramePtr frame;
  assert(bindings.convert(imported, frame) != DIGITOR_RESULT_OK);
  assert(!frame);
  assert(bindings.submit_encoder(frame) == DIGITOR_RESULT_INVALID_ARGUMENT);

  const auto telemetry = bindings.telemetry();
  assert(telemetry.cpu_copies == 0);
  assert(bindings.gpu_only());
  return 0;
}
