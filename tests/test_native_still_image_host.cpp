#include "digitor/native_still_image_host.hpp"
#include "digitor/native_image_runtime.hpp"

#include <cassert>
#include <string>

int main() {
  using namespace digitor;

  assert(native_still_backend_matches_platform(
      NativeStillPlatform::windows, DIGITOR_RENDERER_D3D12));
  assert(native_still_backend_matches_platform(
      NativeStillPlatform::windows, DIGITOR_RENDERER_VULKAN));
  assert(!native_still_backend_matches_platform(
      NativeStillPlatform::windows, DIGITOR_RENDERER_METAL));
  assert(native_still_backend_matches_platform(
      NativeStillPlatform::android, DIGITOR_RENDERER_VULKAN));
  assert(native_still_backend_matches_platform(
      NativeStillPlatform::android, DIGITOR_RENDERER_OPENGL_ES));
  assert(native_still_backend_matches_platform(
      NativeStillPlatform::apple, DIGITOR_RENDERER_METAL));

  for (auto platform : {NativeStillPlatform::windows,
                        NativeStillPlatform::android,
                        NativeStillPlatform::apple}) {
    const auto capabilities = native_image_runtime_capabilities(platform);
    assert(capabilities.jpeg_decode && capabilities.png_decode &&
           capabilities.webp_decode);
    assert(capabilities.jpeg_encode && capabilities.png_encode &&
           capabilities.webp_encode);
    assert(capabilities.exif_orientation && capabilities.icc_metadata &&
           capabilities.alpha && capabilities.tiled_processing &&
           capabilities.terminal_gpu_readback);
    assert(capabilities.codec_path != nullptr);
  }

  NativeStillImageHostConfig invalid;
  invalid.platform = NativeStillPlatform::windows;
  invalid.backend = DIGITOR_RENDERER_METAL;
  auto result = make_native_still_image_host(invalid);
  assert(!result);
  assert(result.result == DIGITOR_RESULT_INVALID_ARGUMENT);

  NativeImageRuntimeConfig runtime_invalid;
  runtime_invalid.platform = NativeStillPlatform::windows;
  runtime_invalid.backend = DIGITOR_RENDERER_D3D12;
  runtime_invalid.context_identity = reinterpret_cast<const void*>(0x1);
  runtime_invalid.device_identity = "test-d3d12-device";
  auto runtime_result = make_native_image_runtime(runtime_invalid);
  assert(!runtime_result);
  assert(runtime_result.result == DIGITOR_RESULT_BACKEND_UNAVAILABLE);

  runtime_invalid.backend = DIGITOR_RENDERER_METAL;
  runtime_result = make_native_image_runtime(runtime_invalid);
  assert(!runtime_result);
  assert(runtime_result.result == DIGITOR_RESULT_INVALID_ARGUMENT);

  NativeStillImageHostConfig config;
  config.platform = NativeStillPlatform::windows;
  config.backend = DIGITOR_RENDERER_D3D12;
  config.context_identity = reinterpret_cast<const void*>(0x1);
  config.device_identity = "test-d3d12-device";
  config.services.decode_to_gpu =
      [](const std::string&, NativeStillImageInfo&, ProcessedGpuFramePtr&,
         std::string&) { return DIGITOR_RESULT_BACKEND_UNAVAILABLE; };
  config.services.resize_gpu =
      [](const ProcessedGpuFramePtr&, std::uint32_t, std::uint32_t,
         std::int64_t, ProcessedGpuFramePtr&, std::string&) {
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      };
  config.services.process_graph =
      [](const GpuImageSessionProcessRequest&, ProcessedGpuFramePtr&,
         std::string&) { return DIGITOR_RESULT_BACKEND_UNAVAILABLE; };
  config.services.encode_from_gpu =
      [](const ProcessedGpuFramePtr&, const std::string&,
         const ImageExportOptions&, const NativeStillImageInfo&,
         const NativeStillImageProgress&) {
        return ImageIoResult{DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                             "test encoder unavailable"};
      };

  result = make_native_still_image_host(std::move(config));
  assert(result);
  assert(result.host.image_io.backend == DIGITOR_RENDERER_D3D12);
  assert(result.host.image_io.context_identity != nullptr);
  assert(gpu_image_session_host_valid(result.host));

  return 0;
}
