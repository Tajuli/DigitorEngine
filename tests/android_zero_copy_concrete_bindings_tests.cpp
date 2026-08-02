#include "digitor/android_zero_copy_concrete_bindings.hpp"
#include "digitor/android_mediacodec_decoder.hpp"

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

  AndroidMediaCodecSessionConfig session{};
  AndroidMediaCodecAhbDecoder native_decoder(session);
  assert(native_decoder.initialize() == DIGITOR_RESULT_INVALID_ARGUMENT);
  session.media_path = "contract-only-does-not-exist.mp4";
  AndroidMediaCodecAhbDecoder host_decoder(session);
#ifndef __ANDROID__
  assert(host_decoder.initialize() == DIGITOR_RESULT_UNSUPPORTED);
  NativeMediaSurfacePtr surface;
  assert(host_decoder.decode_next(surface) == DIGITOR_RESULT_UNSUPPORTED);
#endif
  return 0;
}
