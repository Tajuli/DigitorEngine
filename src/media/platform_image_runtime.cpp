#include "digitor/platform_image_runtime.hpp"

#include <memory>
#include <utility>

namespace digitor {
namespace {

bool codec_complete(const PlatformImageCodecServices& codec) noexcept {
  return static_cast<bool>(codec.decode) && static_cast<bool>(codec.encode);
}

bool gpu_bridge_complete(const PlatformImageGpuBridge& gpu) noexcept {
  return gpu.backend != DIGITOR_RENDERER_AUTO && gpu.context_identity &&
         !gpu.device_identity.empty() && gpu.upload && gpu.resize &&
         gpu.process_graph && gpu.encode;
}

}  // namespace

PlatformImageCapabilities platform_image_capabilities(
    NativeStillPlatform platform) noexcept {
  PlatformImageCapabilities result{};
  result.jpeg_decode = result.jpeg_encode = true;
  result.png_decode = result.png_encode = true;
  result.webp_decode = result.webp_encode = true;
  result.exif_orientation = true;
  result.icc_profile = true;
  result.alpha = true;
  result.tiled_decode = true;
  result.tiled_encode = true;
  switch (platform) {
    case NativeStillPlatform::windows:
      result.codec = PlatformImageCodec::windows_wic;
      break;
    case NativeStillPlatform::android:
      result.codec = PlatformImageCodec::android_image_decoder;
      break;
    case NativeStillPlatform::apple:
      result.codec = PlatformImageCodec::apple_image_io;
      break;
  }
  return result;
}

PlatformImageRuntimeResult make_platform_image_runtime(
    PlatformImageRuntimeConfig config) {
  PlatformImageRuntimeResult result;
  if (!codec_complete(config.codec)) {
    result.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    result.diagnostic = "platform image codec services are incomplete";
    return result;
  }
  if (!gpu_bridge_complete(config.gpu)) {
    result.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    result.diagnostic = "platform GPU image bridge is incomplete";
    return result;
  }
  if (!native_still_backend_matches_platform(config.platform,
                                              config.gpu.backend)) {
    result.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    result.diagnostic = "GPU image bridge backend does not match platform";
    return result;
  }

  auto codec = std::make_shared<PlatformImageCodecServices>(
      std::move(config.codec));
  auto decoded = std::make_shared<PlatformDecodedImage>();
  NativeStillImageHostConfig host{};
  host.platform = config.platform;
  host.backend = config.gpu.backend;
  host.context_identity = config.gpu.context_identity;
  host.device_identity = config.gpu.device_identity;
  host.limits = config.limits;
  host.progress = config.progress;

  host.services.decode_to_gpu =
      [codec, decoded, limits = config.limits,
       progress = config.progress, upload = config.gpu.upload](
          const std::string& path, NativeStillImageInfo& info,
          ProcessedGpuFramePtr& frame, std::string& diagnostic) {
        PlatformDecodedImage local;
        auto decoded_result = codec->decode(path, limits, progress, local);
        if (!decoded_result) {
          diagnostic = std::move(decoded_result.diagnostic);
          return decoded_result.result;
        }
        if (!local.cpu_frame || local.info.display_width == 0 ||
            local.info.display_height == 0) {
          diagnostic = "platform decoder returned an incomplete image";
          return DIGITOR_RESULT_INTERNAL_ERROR;
        }
        auto upload_result = upload(local, 0, frame, diagnostic);
        if (upload_result != DIGITOR_RESULT_OK) {
          // Deliberately no CPU fallback after a GPU runtime was selected.
          return upload_result;
        }
        *decoded = local;
        info = local.info;
        return DIGITOR_RESULT_OK;
      };

  host.services.resize_gpu = std::move(config.gpu.resize);
  host.services.process_graph = std::move(config.gpu.process_graph);
  host.services.encode_from_gpu = std::move(config.gpu.encode);

  result.gpu_host = make_native_still_image_host(std::move(host));
  result.result = result.gpu_host.result;
  result.diagnostic = result.gpu_host.diagnostic;
  result.gpu_locked = static_cast<bool>(result.gpu_host);
  return result;
}

}  // namespace digitor
