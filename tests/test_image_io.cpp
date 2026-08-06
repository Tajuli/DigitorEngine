#include "digitor/gpu_image_session.hpp"
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

  StillImageTimelineCache cache;
  require(!cache.contains("missing"), "empty still-image cache contains a clip");
  const auto empty_clip = cache.register_clip("", "photo.jpg");
  require(empty_clip.result == DIGITOR_RESULT_INVALID_ARGUMENT,
          "empty still-image clip id was accepted");
  MediaDecodeRequest request;
  request.clip_id = "missing";
  request.width = 1920;
  request.height = 1080;
  request.source_time_us = 5000000;
  require(!cache.decode(request), "unregistered still image produced a frame");

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
  require(invalid.result == DIGITOR_RESULT_INVALID_ARGUMENT ||
              invalid.result == DIGITOR_RESULT_UNSUPPORTED,
          "invalid quality produced an unexpected result");

  RenderVideoFrame invalid_frame;
  invalid_frame.width = 2;
  invalid_frame.height = 2;
  const auto invalid_frame_result = export_image_frame(invalid_frame, "invalid-frame.png", {});
  require(invalid_frame_result.result == DIGITOR_RESULT_INVALID_ARGUMENT ||
              invalid_frame_result.result == DIGITOR_RESULT_UNSUPPORTED,
          "invalid frame produced an unexpected result");

  GpuImageSessionHost incomplete_host;
  require(!gpu_image_session_host_valid(incomplete_host),
          "incomplete GPU image-session host was accepted");
  auto [invalid_session, invalid_session_result] =
      GpuImageSession::open("photo.jpg", incomplete_host);
  require(!invalid_session, "invalid GPU image session unexpectedly opened");
  require(invalid_session_result.result == DIGITOR_RESULT_BACKEND_UNAVAILABLE,
          "invalid GPU image-session host returned the wrong result");

  GpuImageSessionProcessRequest process_request;
  require(process_request.mode == GpuImageSessionRenderMode::preview,
          "GPU image-session request did not default to preview mode");
  require(process_request.graph_revision == 0 &&
              process_request.parameter_revision == 0,
          "GPU image-session revisions did not default to zero");
  return 0;
}
