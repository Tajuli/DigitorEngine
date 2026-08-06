#pragma once

#include "digitor/native_still_image_host.hpp"
#include "digitor/image_io.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

enum class PlatformImageCodec : std::uint8_t {
  windows_wic,
  android_image_decoder,
  apple_image_io,
  ffmpeg_cpu,
};

struct PlatformImageCapabilities {
  PlatformImageCodec codec{PlatformImageCodec::ffmpeg_cpu};
  bool jpeg_decode{};
  bool jpeg_encode{};
  bool png_decode{};
  bool png_encode{};
  bool webp_decode{};
  bool webp_encode{};
  bool exif_orientation{};
  bool icc_profile{};
  bool alpha{};
  bool tiled_decode{};
  bool tiled_encode{};
};

struct PlatformDecodedImage {
  NativeStillImageInfo info{};
  std::shared_ptr<VideoFrame> cpu_frame;
  std::string source_color_profile;
};

struct PlatformImageCodecServices {
  PlatformImageCapabilities capabilities{};
  std::function<ImageIoResult(const std::string&, const NativeStillImageLimits&,
                              const NativeStillImageProgress&,
                              PlatformDecodedImage&)>
      decode;
  std::function<ImageIoResult(const PlatformDecodedImage&, const std::string&,
                              const ImageExportOptions&,
                              const NativeStillImageProgress&)>
      encode;
};

// Backend-owned GPU bridge. A selected GPU session must provide every callback.
// The runtime never silently replaces a failed GPU callback with CPU processing.
struct PlatformImageGpuBridge {
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  const void* context_identity{};
  std::string device_identity;
  std::function<DigitorResult(const PlatformDecodedImage&, std::int64_t,
                              ProcessedGpuFramePtr&, std::string&)>
      upload;
  std::function<DigitorResult(const ProcessedGpuFramePtr&, std::uint32_t,
                              std::uint32_t, std::int64_t,
                              ProcessedGpuFramePtr&, std::string&)>
      resize;
  std::function<DigitorResult(const GpuImageSessionProcessRequest&,
                              ProcessedGpuFramePtr&, std::string&)>
      process_graph;
  std::function<ImageIoResult(const ProcessedGpuFramePtr&, const std::string&,
                              const ImageExportOptions&,
                              const NativeStillImageInfo&,
                              const NativeStillImageProgress&)>
      encode;
};

struct PlatformImageRuntimeConfig {
  NativeStillPlatform platform{NativeStillPlatform::windows};
  NativeStillImageLimits limits{};
  NativeStillImageProgress progress{};
  PlatformImageCodecServices codec{};
  PlatformImageGpuBridge gpu{};
};

struct PlatformImageRuntimeResult {
  DigitorResult result{DIGITOR_RESULT_OK};
  std::string diagnostic;
  NativeStillImageHostResult gpu_host{};
  bool gpu_locked{};
  [[nodiscard]] explicit operator bool() const noexcept {
    return result == DIGITOR_RESULT_OK;
  }
};

[[nodiscard]] PlatformImageCapabilities platform_image_capabilities(
    NativeStillPlatform platform) noexcept;

[[nodiscard]] PlatformImageRuntimeResult make_platform_image_runtime(
    PlatformImageRuntimeConfig config);

}  // namespace digitor
