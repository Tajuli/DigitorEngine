#include "digitor/image_io.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
}

int main() {
  using namespace digitor;

  require(supported_still_image_extension("photo.JPG"), "JPG extension rejected");
  require(supported_still_image_extension("photo.jpeg"), "JPEG extension rejected");
  require(supported_still_image_extension("photo.PNG"), "PNG extension rejected");
  require(supported_still_image_extension("photo.webp"), "WebP extension rejected");
  require(!supported_still_image_extension("video.mp4"), "video accepted as still image");
  require(!supported_still_image_extension("image.tiff"), "unsupported TIFF accepted");

  const auto [missing, missing_result] = StillImageAsset::open("");
  require(!missing, "empty image path unexpectedly opened");
  require(missing_result.result == DIGITOR_RESULT_INVALID_ARGUMENT,
          "empty path did not return invalid argument");

  RenderVideoFrame frame;
  frame.width = 2;
  frame.height = 2;
  frame.provenance = "unit-test";
  frame.rgba = {
      1.0F, 0.0F, 0.0F, 1.0F,
      0.0F, 1.0F, 0.0F, 1.0F,
      0.0F, 0.0F, 1.0F, 1.0F,
      1.0F, 1.0F, 1.0F, 0.5F,
  };
  require(frame.valid(), "test image frame is invalid");

  ImageExportOptions invalid_quality;
  invalid_quality.quality = 0;
  invalid_quality.overwrite = true;
  const auto invalid = export_image_frame(frame, "digitor-invalid-quality.jpg", invalid_quality);
#ifdef DIGITOR_HAS_FFMPEG
  require(invalid.result == DIGITOR_RESULT_INVALID_ARGUMENT,
          "invalid image quality was not rejected");
#else
  require(invalid.result == DIGITOR_RESULT_UNSUPPORTED,
          "non-FFmpeg build did not report unsupported image export");
#endif

  RenderVideoFrame gpu_only;
  gpu_only.width = 2;
  gpu_only.height = 2;
  const auto gpu_result = export_image_frame(gpu_only, "gpu-only.png", {});
#ifdef DIGITOR_HAS_FFMPEG
  require(gpu_result.result == DIGITOR_RESULT_INVALID_ARGUMENT,
          "invalid/GPU-only frame was accepted for implicit CPU export");
#else
  require(gpu_result.result == DIGITOR_RESULT_UNSUPPORTED,
          "non-FFmpeg build did not report unsupported image export");
#endif
  return 0;
}
