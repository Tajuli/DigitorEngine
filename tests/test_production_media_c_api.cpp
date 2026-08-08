#include "digitor/digitor.h"
#include "digitor/production_media_c_api.h"

#include <cassert>

int main() {
  DigitorProductionMediaSource* source = nullptr;

  DigitorProductionMediaOptions options{};
  options.struct_size = sizeof(options);
  options.api_version = DIGITOR_PRODUCTION_MEDIA_OPTIONS_VERSION;
  options.hardware_decode = DIGITOR_PRODUCTION_DECODE_AUTO;
  options.allow_cpu_fallback = 1;
  options.cache_capacity = 16;

  assert(digitor_production_media_open(nullptr, &options, &source) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(source == nullptr);
  assert(digitor_production_media_open("", &options, &source) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(source == nullptr);

  auto invalid_options = options;
  invalid_options.api_version = 99;
  assert(digitor_production_media_open("missing.mp4", &invalid_options,
                                       &source) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(source == nullptr);

  invalid_options = options;
  invalid_options.hardware_decode = 99;
  assert(digitor_production_media_open("missing.mp4", &invalid_options,
                                       &source) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(source == nullptr);

  DigitorProductionDecoderInfo info{};
  info.struct_size = sizeof(info);
  info.api_version = DIGITOR_PRODUCTION_DECODER_INFO_VERSION;
  assert(digitor_production_media_get_info(nullptr, &info) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);

  DigitorProductionDecodedFrameInfo frame{};
  frame.struct_size = sizeof(frame);
  frame.api_version = DIGITOR_PRODUCTION_FRAME_INFO_VERSION;
  assert(digitor_production_media_decode(nullptr, 0, &frame) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(digitor_production_media_seek(nullptr, 0) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);

  DigitorProductionNativeSurfaceDescriptor surface{};
  surface.struct_size = sizeof(surface);
  surface.api_version = DIGITOR_PRODUCTION_NATIVE_SURFACE_VERSION;
  assert(digitor_production_media_get_native_surface(nullptr, &surface) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);

  digitor_production_media_close(nullptr);
  return 0;
}
