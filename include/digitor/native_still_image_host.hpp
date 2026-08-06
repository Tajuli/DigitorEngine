#pragma once

#include "digitor/gpu_image_session.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace digitor {

enum class NativeStillPlatform : std::uint8_t {
  windows,
  android,
  apple,
};

enum class ImageOrientation : std::uint8_t {
  normal = 1,
  mirror_horizontal = 2,
  rotate_180 = 3,
  mirror_vertical = 4,
  mirror_horizontal_rotate_270 = 5,
  rotate_90 = 6,
  mirror_horizontal_rotate_90 = 7,
  rotate_270 = 8,
};

struct NativeStillImageInfo {
  std::uint32_t encoded_width{};
  std::uint32_t encoded_height{};
  std::uint32_t display_width{};
  std::uint32_t display_height{};
  ImageOrientation orientation{ImageOrientation::normal};
  bool has_alpha{};
  std::string color_metadata_identity{"srgb"};
};

struct NativeStillImageLimits {
  std::uint32_t max_dimension{32768};
  std::uint64_t max_decoded_bytes{1024ULL * 1024ULL * 1024ULL};
  std::uint32_t tile_width{2048};
  std::uint32_t tile_height{2048};
};

struct NativeStillImageProgress {
  std::function<void(float)> report;
  const std::atomic_bool* cancelled{};
};

struct NativeStillImageServices {
  // Platform codec stage: WIC on Windows, Android ImageDecoder/codec bridge,
  // ImageIO on Apple. The implementation must parse metadata and apply EXIF
  // orientation before returning the native GPU texture.
  std::function<DigitorResult(const std::string&, NativeStillImageInfo&,
                              ProcessedGpuFramePtr&, std::string&)>
      decode_to_gpu;

  // Native GPU resize/transform stage. This must use the same sampling and
  // alpha conventions as video preview/export.
  std::function<DigitorResult(const ProcessedGpuFramePtr&, std::uint32_t,
                              std::uint32_t, std::int64_t,
                              ProcessedGpuFramePtr&, std::string&)>
      resize_gpu;

  // Existing production video node/color/filter/effect executor.
  std::function<DigitorResult(const GpuImageSessionProcessRequest&,
                              ProcessedGpuFramePtr&, std::string&)>
      process_graph;

  // Terminal export stage. A GPU implementation may encode directly or perform
  // an explicit, final staging readback. Processing must not continue on CPU.
  std::function<ImageIoResult(const ProcessedGpuFramePtr&, const std::string&,
                              const ImageExportOptions&,
                              const NativeStillImageInfo&,
                              const NativeStillImageProgress&)>
      encode_from_gpu;
};

struct NativeStillImageHostConfig {
  NativeStillPlatform platform{NativeStillPlatform::windows};
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  const void* context_identity{};
  std::string device_identity;
  NativeStillImageLimits limits{};
  NativeStillImageProgress progress{};
  NativeStillImageServices services{};
};

struct NativeStillImageHostResult {
  DigitorResult result{DIGITOR_RESULT_OK};
  std::string diagnostic;
  GpuImageSessionHost host;
  [[nodiscard]] explicit operator bool() const noexcept {
    return result == DIGITOR_RESULT_OK;
  }
};

[[nodiscard]] bool native_still_backend_matches_platform(
    NativeStillPlatform platform, DigitorRendererBackend backend) noexcept;

[[nodiscard]] NativeStillImageHostResult make_native_still_image_host(
    NativeStillImageHostConfig config);

}  // namespace digitor
