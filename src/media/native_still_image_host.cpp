#include "digitor/native_still_image_host.hpp"
#include "digitor/native_image_runtime.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

namespace digitor {
namespace {

struct HostState {
  NativeStillImageHostConfig config;
  NativeStillImageInfo info;
  std::mutex mutex;
};

bool limits_valid(const NativeStillImageLimits& limits) noexcept {
  return limits.max_dimension != 0 && limits.max_decoded_bytes != 0 &&
         limits.tile_width != 0 && limits.tile_height != 0 &&
         limits.tile_width <= limits.max_dimension &&
         limits.tile_height <= limits.max_dimension;
}

bool cancelled(const NativeStillImageProgress& progress) noexcept {
  return progress.cancelled && progress.cancelled->load();
}

void report(const NativeStillImageProgress& progress, float value) {
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

const char* decoder_for(NativeStillPlatform platform) noexcept {
  switch (platform) {
    case NativeStillPlatform::windows: return "Windows WIC / engine WebP codec";
    case NativeStillPlatform::android: return "Android ImageDecoder / engine codec";
    case NativeStillPlatform::apple: return "Apple ImageIO";
  }
  return "unknown";
}

const char* encoder_for(NativeStillPlatform platform) noexcept {
  switch (platform) {
    case NativeStillPlatform::windows: return "Windows WIC / engine WebP codec";
    case NativeStillPlatform::android: return "Android native image encoder";
    case NativeStillPlatform::apple: return "Apple ImageIO destination";
  }
  return "unknown";
}

}  // namespace

bool native_still_backend_matches_platform(
    NativeStillPlatform platform, DigitorRendererBackend backend) noexcept {
  switch (platform) {
    case NativeStillPlatform::windows:
      return backend == DIGITOR_RENDERER_VULKAN ||
             backend == DIGITOR_RENDERER_D3D12;
    case NativeStillPlatform::android:
      return backend == DIGITOR_RENDERER_VULKAN ||
             backend == DIGITOR_RENDERER_OPENGL_ES;
    case NativeStillPlatform::apple:
      return backend == DIGITOR_RENDERER_METAL;
  }
  return false;
}

NativeStillImageHostResult make_native_still_image_host(
    NativeStillImageHostConfig config) {
  NativeStillImageHostResult result;
  if (!native_still_backend_matches_platform(config.platform, config.backend)) {
    result.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    result.diagnostic = "still-image backend does not match the target platform";
    return result;
  }
  if (!config.context_identity || config.device_identity.empty()) {
    result.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    result.diagnostic = "native GPU context/device identity is incomplete";
    return result;
  }
  if (!limits_valid(config.limits)) {
    result.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    result.diagnostic = "native still-image limits are invalid";
    return result;
  }
  if (!config.services.decode_to_gpu || !config.services.resize_gpu ||
      !config.services.process_graph || !config.services.encode_from_gpu) {
    result.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    result.diagnostic = "native still-image platform services are incomplete";
    return result;
  }

  auto state = std::make_shared<HostState>();
  state->config = std::move(config);

  result.host.image_io.backend = state->config.backend;
  result.host.image_io.context_identity = state->config.context_identity;
  result.host.image_io.device_identity = state->config.device_identity;

  result.host.image_io.decode_and_upload =
      [state](const std::string& path, std::int64_t timestamp,
              std::string& diagnostic)
          -> std::optional<ProcessedGpuFramePtr> {
    if (cancelled(state->config.progress)) {
      diagnostic = "still-image decode cancelled";
      return std::nullopt;
    }
    report(state->config.progress, 0.02F);
    NativeStillImageInfo info;
    ProcessedGpuFramePtr frame;
    const auto decode_result =
        state->config.services.decode_to_gpu(path, info, frame, diagnostic);
    if (decode_result != DIGITOR_RESULT_OK || !frame) {
      if (diagnostic.empty()) diagnostic = "native still-image decode/upload failed";
      return std::nullopt;
    }
    if (info.display_width == 0 || info.display_height == 0 ||
        info.display_width > state->config.limits.max_dimension ||
        info.display_height > state->config.limits.max_dimension) {
      diagnostic = "decoded image dimensions exceed configured limits";
      return std::nullopt;
    }
    if (frame->metadata().width != info.display_width ||
        frame->metadata().height != info.display_height ||
        frame->metadata().timestamp != timestamp) {
      diagnostic = "native decoder frame metadata does not match oriented image";
      return std::nullopt;
    }
    {
      std::lock_guard lock(state->mutex);
      state->info = std::move(info);
    }
    report(state->config.progress, 0.20F);
    return frame;
  };

  result.host.image_io.resize =
      [state](const ProcessedGpuFramePtr& source, std::uint32_t width,
              std::uint32_t height, std::int64_t timestamp,
              ProcessedGpuFramePtr& output, std::string& diagnostic) {
    output.reset();
    if (cancelled(state->config.progress)) {
      diagnostic = "still-image resize cancelled";
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    if (width == 0 || height == 0 ||
        width > state->config.limits.max_dimension ||
        height > state->config.limits.max_dimension) {
      diagnostic = "requested image size exceeds configured limits";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    const auto resize_result = state->config.services.resize_gpu(
        source, width, height, timestamp, output, diagnostic);
    if (resize_result == DIGITOR_RESULT_OK) report(state->config.progress, 0.35F);
    return resize_result;
  };

  result.host.process =
      [state](const GpuImageSessionProcessRequest& request,
              ProcessedGpuFramePtr& output, std::string& diagnostic) {
    output.reset();
    if (cancelled(state->config.progress)) {
      diagnostic = "still-image processing cancelled";
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    const auto process_result =
        state->config.services.process_graph(request, output, diagnostic);
    if (process_result == DIGITOR_RESULT_OK) {
      report(state->config.progress,
             request.mode == GpuImageSessionRenderMode::preview ? 1.0F : 0.80F);
    }
    return process_result;
  };

  result.host.image_io.encode_image =
      [state](const ProcessedGpuFramePtr& frame, const std::string& output_path,
              const ImageExportOptions& options) {
    if (cancelled(state->config.progress)) {
      return ImageIoResult{DIGITOR_RESULT_RESOURCE_IN_USE,
                           "still-image export cancelled"};
    }
    NativeStillImageInfo info;
    {
      std::lock_guard lock(state->mutex);
      info = state->info;
    }
    if (options.format == ImageExportFormat::jpeg && options.preserve_alpha &&
        info.has_alpha) {
      return ImageIoResult{
          DIGITOR_RESULT_INVALID_ARGUMENT,
          "JPEG export cannot preserve alpha; choose a flatten policy first"};
    }
    report(state->config.progress, 0.85F);
    auto encode_result = state->config.services.encode_from_gpu(
        frame, output_path, options, info, state->config.progress);
    if (encode_result) report(state->config.progress, 1.0F);
    return encode_result;
  };

  result.result = DIGITOR_RESULT_OK;
  return result;
}

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
  value.decoder_name = decoder_for(platform);
  value.encoder_name = encoder_for(platform);
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
  if (!limits_valid(config.limits)) {
    output.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    output.diagnostic = "native image runtime limits are invalid";
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
        if (cancelled(progress)) {
          diagnostic = "native image decode cancelled";
          return DIGITOR_RESULT_RESOURCE_IN_USE;
        }
        if (!supported_still_image_extension(path)) {
          diagnostic = "supported image formats are JPEG, PNG and WebP";
          return DIGITOR_RESULT_UNSUPPORTED;
        }
        report(progress, 0.04F);
        auto [asset, open_result] = StillImageAsset::open(path);
        if (!open_result || !asset) {
          diagnostic = open_result.diagnostic.empty()
                           ? "platform image decoder failed"
                           : std::move(open_result.diagnostic);
          return open_result.result;
        }
        auto cpu = asset->render_frame();
        if (!cpu || !cpu->valid() || cpu->gpu_resident()) {
          diagnostic = "platform image decoder did not produce CPU RGBA32F";
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
        report(progress, 0.14F);
        const auto result = upload(*cpu, info, 0, frame, diagnostic);
        if (result == DIGITOR_RESULT_OK && frame) report(progress, 0.20F);
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
        if (cancelled(progress)) {
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
        report(progress, 0.88F);
        const auto result = readback(source, cpu, diagnostic);
        if (result != DIGITOR_RESULT_OK || !cpu.valid() || cpu.gpu_resident()) {
          return ImageIoResult{result == DIGITOR_RESULT_OK
                                   ? DIGITOR_RESULT_INTERNAL_ERROR
                                   : result,
                               diagnostic.empty()
                                   ? "terminal GPU image readback failed"
                                   : std::move(diagnostic)};
        }
        report(progress, 0.94F);
        auto encoded = export_image_frame(cpu, output_path, options);
        if (encoded) report(progress, 1.0F);
        return encoded;
      };

  output.host = make_native_still_image_host(std::move(host));
  output.result = output.host.result;
  output.diagnostic = output.host.diagnostic;
  return output;
}

}  // namespace digitor
