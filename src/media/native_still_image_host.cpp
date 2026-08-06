#include "digitor/native_still_image_host.hpp"

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

bool orientation_value_valid(ImageOrientation orientation) noexcept {
  const auto value = static_cast<std::uint8_t>(orientation);
  return value >= static_cast<std::uint8_t>(ImageOrientation::normal) &&
         value <= static_cast<std::uint8_t>(ImageOrientation::rotate_270);
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

bool native_still_image_info_valid(
    const NativeStillImageInfo& info,
    const NativeStillImageLimits& limits) noexcept {
  if (!limits_valid(limits) || info.encoded_width == 0 ||
      info.encoded_height == 0 || info.display_width == 0 ||
      info.display_height == 0 ||
      info.encoded_width > limits.max_dimension ||
      info.encoded_height > limits.max_dimension ||
      info.display_width > limits.max_dimension ||
      info.display_height > limits.max_dimension ||
      !orientation_value_valid(info.orientation) ||
      info.bits_per_channel == 0 || info.bits_per_channel > 32 ||
      info.color_metadata_identity.empty()) {
    return false;
  }

  constexpr std::uint64_t channels = 4;
  const auto bytes_per_channel =
      static_cast<std::uint64_t>((info.bits_per_channel + 7U) / 8U);
  const auto width = static_cast<std::uint64_t>(info.display_width);
  const auto height = static_cast<std::uint64_t>(info.display_height);
  if (width > std::numeric_limits<std::uint64_t>::max() / height) return false;
  const auto pixels = width * height;
  if (pixels > std::numeric_limits<std::uint64_t>::max() / channels)
    return false;
  const auto components = pixels * channels;
  if (components > std::numeric_limits<std::uint64_t>::max() /
                       bytes_per_channel)
    return false;
  return components * bytes_per_channel <= limits.max_decoded_bytes;
}

std::vector<NativeStillImageTile> make_native_still_tile_plan(
    std::uint32_t width, std::uint32_t height,
    const NativeStillImageLimits& limits) {
  if (!limits_valid(limits) || width == 0 || height == 0 ||
      width > limits.max_dimension || height > limits.max_dimension) {
    return {};
  }

  const auto columns =
      (static_cast<std::uint64_t>(width) + limits.tile_width - 1U) /
      limits.tile_width;
  const auto rows =
      (static_cast<std::uint64_t>(height) + limits.tile_height - 1U) /
      limits.tile_height;
  const auto tile_count64 = columns * rows;
  if (tile_count64 == 0 ||
      tile_count64 > std::numeric_limits<std::uint32_t>::max()) {
    return {};
  }

  const auto tile_count = static_cast<std::uint32_t>(tile_count64);
  std::vector<NativeStillImageTile> tiles;
  tiles.reserve(tile_count);
  std::uint32_t index = 0;
  for (std::uint32_t y = 0; y < height; y += limits.tile_height) {
    for (std::uint32_t x = 0; x < width; x += limits.tile_width) {
      tiles.push_back(NativeStillImageTile{
          x,
          y,
          std::min(limits.tile_width, width - x),
          std::min(limits.tile_height, height - y),
          index++,
          tile_count});
    }
  }
  return tiles;
}

DigitorResult bind_native_still_image_service_pack(
    NativeStillImageHostConfig& config,
    NativeStillImageServicePack pack,
    std::string& diagnostic) {
  diagnostic.clear();
  if (pack.platform != config.platform || pack.backend != config.backend ||
      !native_still_backend_matches_platform(pack.platform, pack.backend)) {
    diagnostic = "native image service pack does not match host platform/backend";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (pack.codec_identity.empty() || !pack.applies_orientation ||
      !pack.preserves_color_metadata || !pack.services.decode_to_gpu ||
      !pack.services.resize_gpu || !pack.services.process_graph ||
      !pack.services.encode_from_gpu) {
    diagnostic = "native image service pack is incomplete";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  config.codec_identity = std::move(pack.codec_identity);
  config.services = std::move(pack.services);
  return DIGITOR_RESULT_OK;
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
  if (config.codec_identity.empty() || !config.services.decode_to_gpu ||
      !config.services.resize_gpu || !config.services.process_graph ||
      !config.services.encode_from_gpu) {
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
    if (!native_still_image_info_valid(info, state->config.limits)) {
      diagnostic = "decoded image metadata or decoded size exceeds configured limits";
      return std::nullopt;
    }
    if (!info.orientation_applied) {
      diagnostic = "native decoder did not apply EXIF orientation";
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
    if (make_native_still_tile_plan(width, height, state->config.limits).empty()) {
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

}  // namespace digitor
