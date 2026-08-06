#pragma once

#include "digitor/native_still_image_host.hpp"
#include "digitor/image_io.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace digitor {

struct NativeImageRuntimeCapabilities {
  NativeStillPlatform platform{NativeStillPlatform::windows};
  bool jpeg_decode{};
  bool png_decode{};
  bool webp_decode{};
  bool jpeg_encode{};
  bool png_encode{};
  bool webp_encode{};
  bool exif_orientation{};
  bool icc_metadata{};
  bool alpha{};
  bool tiled_processing{};
  bool terminal_gpu_readback{};
  const char* codec_path{};
};

// Backend-owned GPU operations. Upload and final_readback are the only allowed
// CPU/GPU boundaries. Once this bridge is selected, processing stays GPU-only.
struct NativeImageGpuBridge {
  std::function<DigitorResult(const RenderVideoFrame&, const NativeStillImageInfo&,
                              std::int64_t, ProcessedGpuFramePtr&, std::string&)>
      upload_rgba32f;
  std::function<DigitorResult(const ProcessedGpuFramePtr&, std::uint32_t,
                              std::uint32_t, std::int64_t,
                              ProcessedGpuFramePtr&, std::string&)>
      resize;
  std::function<DigitorResult(const GpuImageSessionProcessRequest&,
                              ProcessedGpuFramePtr&, std::string&)>
      process_graph;
  std::function<DigitorResult(const ProcessedGpuFramePtr&, RenderVideoFrame&,
                              std::string&)>
      final_readback;
};

struct NativeImageRuntimeConfig {
  NativeStillPlatform platform{NativeStillPlatform::windows};
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  const void* context_identity{};
  std::string device_identity;
  NativeStillImageLimits limits{};
  NativeStillImageProgress progress{};
  NativeImageGpuBridge gpu{};
};

struct NativeImageRuntimeResult {
  DigitorResult result{DIGITOR_RESULT_OK};
  std::string diagnostic;
  NativeStillImageHostResult host;
  NativeImageRuntimeCapabilities capabilities{};
  [[nodiscard]] explicit operator bool() const noexcept {
    return result == DIGITOR_RESULT_OK && static_cast<bool>(host);
  }
};

[[nodiscard]] NativeImageRuntimeCapabilities
native_image_runtime_capabilities(NativeStillPlatform platform) noexcept;

// Builds a production GPU image host over the engine JPEG/PNG/WebP codec layer.
// Decode/orientation/profile conversion happens before one explicit upload;
// export performs one terminal readback and then encodes. No intermediate CPU
// processing is permitted in a GPU-selected session.
[[nodiscard]] NativeImageRuntimeResult make_native_image_runtime(
    NativeImageRuntimeConfig config);

}  // namespace digitor
