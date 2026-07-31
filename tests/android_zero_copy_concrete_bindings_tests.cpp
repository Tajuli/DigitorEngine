#include "digitor/android_zero_copy_concrete_bindings.hpp"

#include <cassert>

int main() {
  using namespace digitor;

  AndroidMediaCodecDecoderConfig decoder{};
  AndroidMediaCodecSurfaceDecoder d(decoder, {});
  assert(d.initialize() == DIGITOR_RESULT_INVALID_ARGUMENT);

  AndroidVulkanExternalImportConfig vk{};
  AndroidVulkanHardwareBufferImporter v(vk);
  assert(v.initialize() == DIGITOR_RESULT_INVALID_ARGUMENT);

  AndroidGlesExternalImageConfig gl{};
  AndroidGlesHardwareBufferImporter g(gl);
  assert(g.initialize() == DIGITOR_RESULT_INVALID_ARGUMENT);

  AndroidGpuYuvConverter converter({});
  AndroidImportedImage image{};
  ProcessedGpuFramePtr frame;
  assert(converter.convert(image, frame) == DIGITOR_RESULT_INVALID_ARGUMENT);

  AndroidHardwareEncoderConfig encoder{};
  AndroidMediaCodecHardwareEncoder e(encoder, {});
  assert(e.initialize() == DIGITOR_RESULT_INVALID_ARGUMENT);

  AndroidHardwareBufferFrame p010{};
  p010.hardware_buffer = reinterpret_cast<void*>(1);
  p010.width = 1920;
  p010.height = 1080;
  p010.format = AndroidYuvFormat::p010;
  p010.bit_depth = 10;
  p010.timestamp_us = 1;
  p010.frame_identity = 1;
  assert(p010.bit_depth == 10);
  return 0;
}
