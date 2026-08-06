#pragma once

#include "digitor/native_still_image_host.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace digitor {

enum class NativeImageCodec : std::uint8_t { jpeg, png, webp };
enum class NativeImageAlphaMode : std::uint8_t { opaque, straight, premultiplied };

struct NativeImageCodecCapabilities {
  bool decode_jpeg{};
  bool decode_png{};
  bool decode_webp{};
  bool encode_jpeg{};
  bool encode_png{};
  bool encode_webp{};
  bool exif_orientation{};
  bool icc_profile{};
  bool alpha{};
  bool tiled_decode{};
  bool tiled_encode{};
};

struct NativeImageTile {
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};
};

struct FlutterNativeImageDescriptor {
  std::uint64_t texture_identity{};
  std::uint64_t context_identity{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t backend{};
  std::uint32_t pixel_format{};
  std::uint32_t alpha_mode{};
  std::uint32_t reserved{};
};

struct NativeImagePlatformServices {
  NativeStillPlatform platform{NativeStillPlatform::windows};
  NativeImageCodecCapabilities capabilities{};
  NativeStillImageServices gpu_services{};
  std::function<ImageIoResult(const std::string&, NativeStillImageInfo&)>
      inspect_metadata;
};

struct NativeImageRuntimeConfig {
  NativeStillImageHostConfig host{};
  NativeImagePlatformServices platform_services{};
  bool require_exif{true};
  bool require_icc{true};
  bool require_alpha{true};
};

struct NativeImageRuntimeResult {
  DigitorResult result{DIGITOR_RESULT_OK};
  std::string diagnostic;
  NativeStillImageHostResult host{};
  [[nodiscard]] explicit operator bool() const noexcept {
    return result == DIGITOR_RESULT_OK;
  }
};

[[nodiscard]] inline bool native_image_codec_supported(
    const NativeImageCodecCapabilities& c, NativeImageCodec codec,
    bool encode) noexcept {
  switch (codec) {
    case NativeImageCodec::jpeg: return encode ? c.encode_jpeg : c.decode_jpeg;
    case NativeImageCodec::png: return encode ? c.encode_png : c.decode_png;
    case NativeImageCodec::webp: return encode ? c.encode_webp : c.decode_webp;
  }
  return false;
}

[[nodiscard]] inline std::vector<NativeImageTile> plan_native_image_tiles(
    std::uint32_t width, std::uint32_t height,
    const NativeStillImageLimits& limits) {
  if (!width || !height || !limits.tile_width || !limits.tile_height ||
      width > limits.max_dimension || height > limits.max_dimension) {
    throw std::invalid_argument("native image tile request exceeds limits");
  }
  const auto pixels = static_cast<std::uint64_t>(width) * height;
  if (pixels > std::numeric_limits<std::uint64_t>::max() / 16ULL ||
      pixels * 16ULL > limits.max_decoded_bytes) {
    throw std::length_error("native image decoded footprint exceeds limit");
  }
  std::vector<NativeImageTile> tiles;
  const auto columns = (width + limits.tile_width - 1U) / limits.tile_width;
  const auto rows = (height + limits.tile_height - 1U) / limits.tile_height;
  tiles.reserve(static_cast<std::size_t>(columns) * rows);
  for (std::uint32_t y = 0; y < height; y += limits.tile_height) {
    for (std::uint32_t x = 0; x < width; x += limits.tile_width) {
      tiles.push_back({x, y, std::min(limits.tile_width, width - x),
                       std::min(limits.tile_height, height - y)});
    }
  }
  return tiles;
}

[[nodiscard]] inline NativeImageRuntimeResult make_native_image_runtime(
    NativeImageRuntimeConfig config) {
  NativeImageRuntimeResult out;
  const auto& c = config.platform_services.capabilities;
  if (config.platform_services.platform != config.host.platform) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "native image platform service does not match host";
    return out;
  }
  for (const auto codec : {NativeImageCodec::jpeg, NativeImageCodec::png,
                           NativeImageCodec::webp}) {
    if (!native_image_codec_supported(c, codec, false) ||
        !native_image_codec_supported(c, codec, true)) {
      out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      out.diagnostic = "JPEG/PNG/WebP native codec set is incomplete";
      return out;
    }
  }
  if ((config.require_exif && !c.exif_orientation) ||
      (config.require_icc && !c.icc_profile) ||
      (config.require_alpha && !c.alpha)) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "required EXIF/ICC/alpha capability is unavailable";
    return out;
  }
  if (!c.tiled_decode || !c.tiled_encode) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "large-image tiled codec capability is unavailable";
    return out;
  }
  if (!config.platform_services.inspect_metadata) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = "native image metadata inspector is unavailable";
    return out;
  }
  config.host.services = std::move(config.platform_services.gpu_services);
  out.host = make_native_still_image_host(std::move(config.host));
  out.result = out.host.result;
  out.diagnostic = out.host.diagnostic;
  return out;
}

[[nodiscard]] inline FlutterNativeImageDescriptor flutter_native_image_descriptor(
    const ProcessedGpuFramePtr& frame) noexcept {
  FlutterNativeImageDescriptor out{};
  if (!frame || !frame->ready() || !frame->context_live()) return out;
  const auto& m = frame->metadata();
  out.texture_identity = frame->identity();
  out.width = m.width;
  out.height = m.height;
  out.backend = static_cast<std::uint32_t>(frame->backend());
  out.pixel_format = static_cast<std::uint32_t>(m.format);
  out.alpha_mode = static_cast<std::uint32_t>(m.alpha);
  return out;
}

}  // namespace digitor
