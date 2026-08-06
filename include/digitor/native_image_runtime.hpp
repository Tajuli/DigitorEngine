#pragma once

#include "digitor/native_still_image_host.hpp"
#include "digitor/image_io.hpp"

#include <cstdint>
#include <functional>
#include <memory>
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
  const char* decoder_name{};
  const char* encoder_name{};
};

// Backend-owned operations. Upload/readback are explicit terminal boundaries;
// implementations must not silently switch a GPU-selected session to CPU.
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

// Creates the complete platform image runtime. Decode uses the engine image
// codec layer, then performs an explicit backend upload. Export performs one
// terminal GPU readback and encodes through the same JPEG/PNG/WebP codec layer.
// Processing between upload and final readback remains GPU-only.
[[nodiscard]] NativeImageRuntimeResult make_native_image_runtime(
    NativeImageRuntimeConfig config);

}  // namespace digitor
