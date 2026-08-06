#pragma once

#include "digitor/digitor.h"
#include "digitor/media.hpp"
#include "digitor/timeline_media_adapter.hpp"
#include "digitor/timeline_render_runtime.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

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

class StillImageAsset {
 public:
  static std::pair<std::shared_ptr<StillImageAsset>, ImageIoResult> open(
      const std::string& path);

  [[nodiscard]] std::uint32_t width() const noexcept;
  [[nodiscard]] std::uint32_t height() const noexcept;
  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  [[nodiscard]] std::shared_ptr<const VideoFrame> frame() const noexcept;

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

// Owns retained still-image assets for timeline clips. Register each image clip
// once and install decoder_callback() as MediaAdapterCallbacks::decode_still_image.
class StillImageTimelineCache {
 public:
  ImageIoResult register_clip(std::string clip_id, const std::string& path);
  void unregister_clip(const std::string& clip_id) noexcept;
  void clear() noexcept;
  [[nodiscard]] bool contains(const std::string& clip_id) const noexcept;
  [[nodiscard]] std::optional<RenderVideoFrame> decode(
      const MediaDecodeRequest& request) const;
  [[nodiscard]] std::function<std::optional<RenderVideoFrame>(const MediaDecodeRequest&)>
  decoder_callback();

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<StillImageAsset>> clips_;
};

ImageIoResult export_image_frame(const RenderVideoFrame& frame,
                                 const std::string& output_path,
                                 const ImageExportOptions& options = {});

[[nodiscard]] bool image_io_available() noexcept;
[[nodiscard]] bool supported_still_image_extension(const std::string& path) noexcept;

}  // namespace digitor
