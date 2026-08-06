#include "digitor/native_image_runtime.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace digitor {
namespace {

bool runtime_cancelled(const NativeStillImageProgress& progress) noexcept {
  return progress.cancelled && progress.cancelled->load();
}

void runtime_report(const NativeStillImageProgress& progress, float value) {
  if (progress.report) progress.report(std::clamp(value, 0.0F, 1.0F));
}

bool decoded_size_allowed(std::uint32_t width, std::uint32_t height,
                          const NativeStillImageLimits& limits) noexcept {
  if (width == 0 || height == 0 || width > limits.max_dimension ||
      height > limits.max_dimension) return false;
  constexpr std::uint64_t bytes_per_pixel = sizeof(float) * 4ULL;
  if (width > std::numeric_limits<std::uint64_t>::max() / height) return false;
  const auto pixels = static_cast<std::uint64_t>(width) * height;
  return pixels <= limits.max_decoded_bytes / bytes_per_pixel;
}

bool frame_has_alpha(const RenderVideoFrame& frame) noexcept {
  if (!frame.valid() || frame.gpu_resident()) return false;
  for (std::size_t index = 3; index < frame.rgba.size(); index += 4) {
    if (frame.rgba[index] < 1.0F) return true;
  }
  return false;
}

const char* codec_path_for(NativeStillPlatform platform) noexcept {
  switch (platform) {
    case NativeStillPlatform::windows:
      return "engine JPEG/PNG/WebP codec + D3D12/Vulkan upload";
    case NativeStillPlatform::android:
      return "engine JPEG/PNG/WebP codec + Vulkan/GLES upload";
    case NativeStillPlatform::apple:
      return "engine JPEG/PNG/WebP codec + Metal upload";
  }
  return "unknown";
}

}  // namespace

NativeImageRuntimeCapabilities native_image_runtime_capabilities(
    NativeStillPlatform platform) noexcept {
  NativeImageRuntimeCapabilities value;
  value.platform = platform;
  value.jpeg_decode = value.png_decode = value.webp_decode = true;
  value.jpeg_encode = value.png_encode = value.webp_encode = true;
  value.exif_orientation = true;
  value.icc_metadata = true;
  value.alpha = true;
  value.tiled_processing = true;
  value.terminal_gpu_readback = true;
  value.codec_path = codec_path_for(platform);
  return value;
}

NativeImageRuntimeResult make_native_image_runtime(
    NativeImageRuntimeConfig config) {
  NativeImageRuntimeResult output;
  output.capabilities = native_image_runtime_capabilities(config.platform);

  if (!native_still_backend_matches_platform(config.platform, config.backend)) {
    output.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    output.diagnostic = "native image runtime backend does not match platform";
    return output;
  }
  if (!config.context_identity || config.device_identity.empty()) {
    output.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    output.diagnostic = "native image runtime requires GPU context identity";
    return output;
  }
  if (!config.gpu.upload_rgba32f || !config.gpu.resize ||
      !config.gpu.process_graph || !config.gpu.final_readback) {
    output.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    output.diagnostic = "native image GPU bridge is incomplete";
    return output;
  }

  NativeStillImageHostConfig host;
  host.platform = config.platform;
  host.backend = config.backend;
  host.context_identity = config.context_identity;
  host.device_identity = config.device_identity;
  host.limits = config.limits;
  host.progress = config.progress;

  const auto limits = config.limits;
  const auto progress = config.progress;
  const auto upload = config.gpu.upload_rgba32f;
  host.services.decode_to_gpu =
      [limits, progress, upload](const std::string& path,
                                 NativeStillImageInfo& info,
                                 ProcessedGpuFramePtr& frame,
                                 std::string& diagnostic) {
        frame.reset();
        if (runtime_cancelled(progress)) {
          diagnostic = "native image decode cancelled";
          return DIGITOR_RESULT_RESOURCE_IN_USE;
        }
        if (!supported_still_image_extension(path)) {
          diagnostic = "supported image formats are JPEG, PNG and WebP";
          return DIGITOR_RESULT_UNSUPPORTED;
        }
        runtime_report(progress, 0.04F);
        auto [asset, open_result] = StillImageAsset::open(path);
        if (!open_result || !asset) {
          diagnostic = open_result.diagnostic.empty()
                           ? "image codec decode failed"
                           : std::move(open_result.diagnostic);
          return open_result.result;
        }
        auto cpu = asset->render_frame();
        if (!cpu || !cpu->valid() || cpu->gpu_resident()) {
          diagnostic = "image codec did not produce CPU RGBA32F";
          return DIGITOR_RESULT_INTERNAL_ERROR;
        }
        if (!decoded_size_allowed(cpu->width, cpu->height, limits)) {
          diagnostic = "decoded image exceeds configured dimension/memory limits";
          return DIGITOR_RESULT_OUT_OF_MEMORY;
        }
        info.encoded_width = info.display_width = cpu->width;
        info.encoded_height = info.display_height = cpu->height;
        info.orientation = ImageOrientation::normal;
        info.has_alpha = frame_has_alpha(*cpu);
        info.color_metadata_identity = "decoded-source-profile";
        runtime_report(progress, 0.14F);
        const auto result = upload(*cpu, info, 0, frame, diagnostic);
        if (result == DIGITOR_RESULT_OK && frame) runtime_report(progress, 0.20F);
        return result;
      };

  host.services.resize_gpu = std::move(config.gpu.resize);
  host.services.process_graph = std::move(config.gpu.process_graph);
  const auto readback = std::move(config.gpu.final_readback);
  host.services.encode_from_gpu =
      [readback](const ProcessedGpuFramePtr& source,
                 const std::string& output_path,
                 const ImageExportOptions& options,
                 const NativeStillImageInfo& info,
                 const NativeStillImageProgress& progress) {
        if (runtime_cancelled(progress)) {
          return ImageIoResult{DIGITOR_RESULT_RESOURCE_IN_USE,
                               "native image export cancelled"};
        }
        if (options.format == ImageExportFormat::jpeg && options.preserve_alpha &&
            info.has_alpha) {
          return ImageIoResult{DIGITOR_RESULT_INVALID_ARGUMENT,
                               "JPEG requires alpha flattening"};
        }
        RenderVideoFrame cpu;
        std::string diagnostic;
        runtime_report(progress, 0.88F);
        const auto result = readback(source, cpu, diagnostic);
        if (result != DIGITOR_RESULT_OK || !cpu.valid() || cpu.gpu_resident()) {
          return ImageIoResult{result == DIGITOR_RESULT_OK
                                   ? DIGITOR_RESULT_INTERNAL_ERROR
                                   : result,
                               diagnostic.empty()
                                   ? "terminal GPU image readback failed"
                                   : std::move(diagnostic)};
        }
        runtime_report(progress, 0.94F);
        auto encoded = export_image_frame(cpu, output_path, options);
        if (encoded) runtime_report(progress, 1.0F);
        return encoded;
      };

  output.host = make_native_still_image_host(std::move(host));
  output.result = output.host.result;
  output.diagnostic = output.host.diagnostic;
  return output;
}

}  // namespace digitor
