#include "digitor/gpu_image_session.hpp"
#include "digitor/gpu_image_session_c_api.h"
#include "digitor/image_io.hpp"
#include "digitor/still_image_runtime.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {
void require(bool value, const char* message) {
  if (!value) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

struct CApiHostState {
  int opens{};
  int processes{};
  int exports{};
  int releases{};
};

DigitorNativeGpuTextureDescriptor make_texture(uint64_t handle,
                                               uint32_t width,
                                               uint32_t height,
                                               int64_t timestamp) {
  DigitorNativeGpuTextureDescriptor value{};
  value.struct_size = sizeof(value);
  value.api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
  value.backend = DIGITOR_NATIVE_TEXTURE_BACKEND_VULKAN;
  value.handle_type = DIGITOR_NATIVE_TEXTURE_HANDLE_VK_IMAGE;
  value.native_handle = handle;
  value.width = width;
  value.height = height;
  value.pixel_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  value.timestamp_us = timestamp;
  value.generation = handle;
  value.device_identity = 41;
  value.context_identity = 42;
  value.readiness = DIGITOR_NATIVE_TEXTURE_READY;
  return value;
}

DigitorResult c_open(void* user_data, const char*,
                     DigitorNativeGpuTextureDescriptor* out,
                     char*, uint32_t) {
  auto& state = *static_cast<CApiHostState*>(user_data);
  ++state.opens;
  *out = make_texture(100, 1920, 1080, 0);
  return DIGITOR_RESULT_OK;
}

DigitorResult c_process(void* user_data, DigitorGpuImageRenderMode,
                        const DigitorNativeGpuTextureDescriptor*,
                        uint32_t width, uint32_t height,
                        int64_t timestamp_us, uint64_t, uint64_t,
                        DigitorNativeGpuTextureDescriptor* out,
                        char*, uint32_t) {
  auto& state = *static_cast<CApiHostState*>(user_data);
  ++state.processes;
  *out = make_texture(static_cast<uint64_t>(200 + state.processes),
                      width, height, timestamp_us);
  return DIGITOR_RESULT_OK;
}

DigitorResult c_export(void* user_data,
                       const DigitorNativeGpuTextureDescriptor* processed,
                       const char*, const DigitorImageExportOptions*,
                       char*, uint32_t) {
  auto& state = *static_cast<CApiHostState*>(user_data);
  ++state.exports;
  return processed && processed->native_handle != 0
             ? DIGITOR_RESULT_OK
             : DIGITOR_RESULT_INVALID_ARGUMENT;
}

void c_release(void* user_data,
               const DigitorNativeGpuTextureDescriptor*) {
  ++static_cast<CApiHostState*>(user_data)->releases;
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

  auto exact = StillImageRuntime::compare_cpu_frames(frame, frame);
  require(exact.compared && exact.equivalent && exact.max_absolute_error == 0.0F,
          "identical still-image frames failed per-pixel parity");
  auto changed = frame;
  changed.rgba[0] += 0.01F;
  auto mismatch = StillImageRuntime::compare_cpu_frames(frame, changed);
  require(mismatch.compared && !mismatch.equivalent &&
              mismatch.failing_components == 1,
          "per-pixel mismatch was not detected");

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

  CApiHostState c_state;
  DigitorGpuImageSessionHost c_host{};
  c_host.struct_size = sizeof(c_host);
  c_host.api_version = DIGITOR_GPU_IMAGE_SESSION_HOST_VERSION;
  c_host.user_data = &c_state;
  c_host.required_device_identity = 41;
  c_host.required_context_identity = 42;
  c_host.open_image = c_open;
  c_host.process_image = c_process;
  c_host.export_image = c_export;
  c_host.release_texture = c_release;

  DigitorGpuImageSession* c_session = nullptr;
  require(digitor_gpu_image_session_create(&c_host, "photo.png", &c_session) ==
              DIGITOR_RESULT_OK,
          "GPU image C session did not open");
  require(c_session != nullptr && c_state.opens == 1,
          "GPU image C session open callback was not retained");

  DigitorNativeGpuTextureDescriptor preview{};
  require(digitor_gpu_image_session_render(
              c_session, DIGITOR_GPU_IMAGE_RENDER_PREVIEW, 1280, 720, 12,
              &preview) == DIGITOR_RESULT_OK,
          "GPU image C preview failed");
  require(preview.width == 1280 && preview.height == 720 &&
              c_state.processes == 1,
          "GPU image C preview descriptor is invalid");

  DigitorNativeGpuTextureDescriptor cached{};
  require(digitor_gpu_image_session_render(
              c_session, DIGITOR_GPU_IMAGE_RENDER_PREVIEW, 1280, 720, 12,
              &cached) == DIGITOR_RESULT_OK,
          "GPU image C cached preview failed");
  require(cached.native_handle == preview.native_handle &&
              c_state.processes == 1,
          "GPU image C cache did not retain the processed frame");

  require(digitor_gpu_image_session_set_parameter_revision(c_session, 1) ==
              DIGITOR_RESULT_OK,
          "GPU image C parameter revision failed");
  require(digitor_gpu_image_session_render(
              c_session, DIGITOR_GPU_IMAGE_RENDER_PREVIEW, 1280, 720, 12,
              &cached) == DIGITOR_RESULT_OK && c_state.processes == 2,
          "GPU image C parameter revision did not invalidate the cache");

  DigitorImageExportOptions export_options{};
  export_options.struct_size = sizeof(export_options);
  export_options.api_version = DIGITOR_IMAGE_EXPORT_OPTIONS_VERSION;
  export_options.format = DIGITOR_IMAGE_EXPORT_PNG;
  export_options.quality = 100;
  export_options.preserve_alpha = 1;
  require(digitor_gpu_image_session_export(c_session, "out.png",
                                            &export_options) ==
              DIGITOR_RESULT_OK && c_state.exports == 1,
          "GPU image C export failed");

  require(digitor_gpu_image_session_destroy(c_session) == DIGITOR_RESULT_OK,
          "GPU image C session destroy failed");
  require(digitor_gpu_image_session_destroy(c_session) ==
              DIGITOR_RESULT_RESOURCE_IN_USE,
          "GPU image C double destroy was not rejected");
  require(digitor_gpu_image_session_render(
              c_session, DIGITOR_GPU_IMAGE_RENDER_PREVIEW, 1, 1, 0,
              &cached) == DIGITOR_RESULT_RESOURCE_IN_USE,
          "GPU image C stale handle was not rejected");

  uint32_t diagnostic_size = 0;
  require(digitor_gpu_image_session_get_last_error(
              c_session, nullptr, &diagnostic_size) == DIGITOR_RESULT_OK &&
              diagnostic_size > 1,
          "GPU image C diagnostic size query failed");
  char diagnostic[128]{};
  uint32_t diagnostic_capacity = sizeof(diagnostic);
  require(digitor_gpu_image_session_get_last_error(
              c_session, diagnostic, &diagnostic_capacity) ==
              DIGITOR_RESULT_OK &&
              std::strstr(diagnostic, "retired") != nullptr,
          "GPU image C stale-handle diagnostic is missing");
  require(c_state.releases >= 3,
          "GPU image C texture ownership was not released");
  return 0;
}
