#pragma once

#include "digitor/native_still_image_host.hpp"

#include <cstdint>
#include <functional>
#include <string>
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

[[nodiscard]] bool native_image_codec_supported(
    const NativeImageCodecCapabilities&, NativeImageCodec codec,
    bool encode) noexcept;

[[nodiscard]] std::vector<NativeImageTile> plan_native_image_tiles(
    std::uint32_t width, std::uint32_t height,
    const NativeStillImageLimits& limits);

[[nodiscard]] NativeImageRuntimeResult make_native_image_runtime(
    NativeImageRuntimeConfig config);

[[nodiscard]] FlutterNativeImageDescriptor flutter_native_image_descriptor(
    const ProcessedGpuFramePtr& frame) noexcept;

}  // namespace digitor
