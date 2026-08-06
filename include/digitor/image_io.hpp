#pragma once

#include "digitor/digitor.h"
#include "digitor/media.hpp"
#include "digitor/timeline_render_runtime.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace digitor {

enum class ImageExportFormat : std::uint8_t { jpeg, png, webp };

struct ImageExportOptions {
  ImageExportFormat format{ImageExportFormat::jpeg};
  int quality{90};
  std::uint32_t width{};   // 0 keeps source width.
  std::uint32_t height{};  // 0 keeps source height.
  bool preserve_alpha{true};
  bool overwrite{false};
};

struct ImageIoResult {
  DigitorResult result{DIGITOR_RESULT_OK};
  std::string diagnostic;
  [[nodiscard]] explicit operator bool() const noexcept {
    return result == DIGITOR_RESULT_OK;
  }
};

// Decodes a JPEG, PNG or WebP through the engine's FFmpeg media provider and
// retains a full-resolution linear RGBA frame. The retained frame is reused for
// every timeline timestamp; still images are never repeatedly decoded as video.
class StillImageAsset {
 public:
  static std::pair<std::shared_ptr<StillImageAsset>, ImageIoResult> open(
      const std::string& path);

  [[nodiscard]] std::uint32_t width() const noexcept;
  [[nodiscard]] std::uint32_t height() const noexcept;
  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  [[nodiscard]] std::shared_ptr<const VideoFrame> frame() const noexcept;

  // Returns a CPU linear-RGBA timeline frame. Width/height 0 use the original
  // dimensions. A scaled result is cached, making long still-image clips cheap.
  [[nodiscard]] std::optional<RenderVideoFrame> render_frame(
      std::uint32_t width = 0, std::uint32_t height = 0) const;

  ImageIoResult export_image(const std::string& output_path,
                             const ImageExportOptions& options = {}) const;

 private:
  StillImageAsset(std::string path, std::shared_ptr<VideoFrame> frame);

  std::string path_;
  std::shared_ptr<VideoFrame> frame_;
  mutable std::mutex cache_mutex_;
  mutable std::optional<RenderVideoFrame> scaled_cache_;
};

// Stateless helpers for photo-workspace export and processed timeline frames.
ImageIoResult export_image_frame(const RenderVideoFrame& frame,
                                 const std::string& output_path,
                                 const ImageExportOptions& options = {});

[[nodiscard]] bool image_io_available() noexcept;
[[nodiscard]] bool supported_still_image_extension(const std::string& path) noexcept;

}  // namespace digitor
