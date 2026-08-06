#pragma once

#include "digitor/native_still_image_host.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace digitor {

enum class NativeImagePixelFormat : std::uint8_t {
  rgba8,
  bgra8,
  rgba16,
  rgba32_float,
};

struct NativeImageBuffer {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t row_bytes{};
  NativeImagePixelFormat format{NativeImagePixelFormat::rgba8};
  bool premultiplied_alpha{};
  std::vector<std::uint8_t> pixels;
};

struct NativeImageCodecCapabilities {
  bool jpeg_decode{};
  bool jpeg_encode{};
  bool png_decode{};
  bool png_encode{};
  bool webp_decode{};
  bool webp_encode{};
  bool exif_orientation{};
  bool icc_profile{};
  bool alpha{};
  bool region_decode{};
  const char* implementation{"unavailable"};
};

struct NativeImageDecodeRequest {
  std::string path;
  NativeStillImageLimits limits{};
  NativeStillImageProgress progress{};
  bool apply_orientation{true};
  bool preserve_color_profile{true};
};

struct NativeImageEncodeRequest {
  std::string path;
  ImageExportOptions options{};
  NativeStillImageInfo source_info{};
  NativeStillImageProgress progress{};
  std::uint32_t jpeg_background_rgba{0xff000000U};
};

class NativeImageCodec {
 public:
  virtual ~NativeImageCodec() = default;
  [[nodiscard]] virtual NativeImageCodecCapabilities capabilities() const noexcept = 0;
  virtual ImageIoResult decode(const NativeImageDecodeRequest& request,
                               NativeStillImageInfo& info,
                               NativeImageBuffer& output) noexcept = 0;
  virtual ImageIoResult encode(const NativeImageEncodeRequest& request,
                               const NativeImageBuffer& input) noexcept = 0;
};

// Returns the concrete platform codec when it is compiled into the current
// target. The factory never substitutes a different platform implementation.
[[nodiscard]] std::unique_ptr<NativeImageCodec>
create_native_image_codec(NativeStillPlatform platform) noexcept;

[[nodiscard]] bool validate_native_image_buffer(
    const NativeImageBuffer& image, const NativeStillImageLimits& limits,
    std::string& diagnostic) noexcept;

// Deterministic EXIF orientation transform. This is shared by CPU fallback and
// platform codecs which cannot request oriented output directly.
[[nodiscard]] ImageIoResult apply_image_orientation(
    NativeImageBuffer& image, ImageOrientation orientation,
    const NativeStillImageProgress& progress) noexcept;

// Converts straight/premultiplied alpha and JPEG flattening without changing
// RGB operation order. Work is split through the process-wide CPU executor.
[[nodiscard]] ImageIoResult prepare_image_for_export(
    NativeImageBuffer& image, const NativeImageEncodeRequest& request) noexcept;

}  // namespace digitor
