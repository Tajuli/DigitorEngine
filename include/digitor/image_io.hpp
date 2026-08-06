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

#if !defined(DIGITOR_HAS_FFMPEG)
namespace {
// image_io.cpp defines this helper for the FFmpeg encoder path. In builds where
// FFmpeg is disabled the definition is intentionally retained for source
// symmetry, so mark the entity maybe-unused without weakening -Werror globally.
[[maybe_unused]] float clamp01(float value) noexcept;
}  // namespace
#endif

enum class ImageExportFormat : std::uint8_t { jpeg, png, webp };

struct ImageExportOptions {
  ImageExportFormat format{ImageExportFormat::jpeg};
  int quality{90};
  std::uint32_t width{};
  std::uint32_t height{};
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

// Register each image clip once and assign decoder_callback() to
// MediaAdapterCallbacks::decode_still_image. The decoder ignores timeline source
// time and reuses the retained decoded frame throughout the clip duration.
class StillImageTimelineCache {
 public:
  ImageIoResult register_clip(std::string clip_id, const std::string& path) {
    if (clip_id.empty()) return {DIGITOR_RESULT_INVALID_ARGUMENT, "clip id is empty"};
    auto [asset, result] = StillImageAsset::open(path);
    if (!result) return result;
    std::lock_guard lock(mutex_);
    clips_.insert_or_assign(std::move(clip_id), std::move(asset));
    return {};
  }

  void unregister_clip(const std::string& clip_id) noexcept {
    std::lock_guard lock(mutex_);
    clips_.erase(clip_id);
  }

  void clear() noexcept {
    std::lock_guard lock(mutex_);
    clips_.clear();
  }

  [[nodiscard]] bool contains(const std::string& clip_id) const noexcept {
    std::lock_guard lock(mutex_);
    return clips_.contains(clip_id);
  }

  [[nodiscard]] std::optional<RenderVideoFrame> decode(
      const MediaDecodeRequest& request) const {
    std::shared_ptr<StillImageAsset> asset;
    {
      std::lock_guard lock(mutex_);
      const auto found = clips_.find(request.clip_id);
      if (found == clips_.end()) return std::nullopt;
      asset = found->second;
    }
    return asset->render_frame(request.width, request.height);
  }

  [[nodiscard]] std::function<std::optional<RenderVideoFrame>(const MediaDecodeRequest&)>
  decoder_callback() {
    return [this](const MediaDecodeRequest& request) { return decode(request); };
  }

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
