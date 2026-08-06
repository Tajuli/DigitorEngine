#pragma once

#include "digitor/image_io.hpp"
#include "digitor/timeline_media_adapter.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace digitor {

struct GpuStillImageHost {
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  const void* context_identity{};
  std::string device_identity;
  std::function<std::optional<ProcessedGpuFramePtr>(const std::string&, std::int64_t,
                                                     std::string&)> decode_and_upload;
  std::function<DigitorResult(const ProcessedGpuFramePtr&, std::uint32_t,
                              std::uint32_t, std::int64_t,
                              ProcessedGpuFramePtr&, std::string&)> resize;
  std::function<ImageIoResult(const ProcessedGpuFramePtr&, const std::string&,
                              const ImageExportOptions&)> encode_image;
};

inline bool gpu_still_host_valid(const GpuStillImageHost& host) noexcept {
  return host.backend != DIGITOR_RENDERER_AUTO && host.context_identity != nullptr &&
         static_cast<bool>(host.decode_and_upload) && static_cast<bool>(host.resize);
}

inline bool gpu_still_frame_compatible(const ProcessedGpuFramePtr& frame,
                                       const GpuStillImageHost& host) noexcept {
  return frame && frame->backend() == host.backend &&
         frame->has_context_identity(host.context_identity) &&
         frame->context_live() && frame->ready();
}

class GpuStillImageAsset final {
 public:
  static std::pair<std::shared_ptr<GpuStillImageAsset>, ImageIoResult> open(
      std::string path, GpuStillImageHost host) {
    if (path.empty()) {
      return {nullptr, {DIGITOR_RESULT_INVALID_ARGUMENT, "image path is empty"}};
    }
    if (!supported_still_image_extension(path)) {
      return {nullptr, {DIGITOR_RESULT_UNSUPPORTED,
                        "supported still-image extensions are JPEG, PNG and WebP"}};
    }
    if (!gpu_still_host_valid(host)) {
      return {nullptr, {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                        "GPU still-image host is incomplete"}};
    }
    std::string diagnostic;
    auto source = host.decode_and_upload(path, 0, diagnostic);
    if (!source || !gpu_still_frame_compatible(*source, host)) {
      if (diagnostic.empty()) {
        diagnostic = "decoder did not return a compatible GPU-resident frame";
      }
      return {nullptr, {DIGITOR_RESULT_BACKEND_UNAVAILABLE, std::move(diagnostic)}};
    }
    if ((*source)->metadata().width == 0 || (*source)->metadata().height == 0) {
      return {nullptr, {DIGITOR_RESULT_INTERNAL_ERROR,
                        "GPU still-image frame has invalid dimensions"}};
    }
    return {std::shared_ptr<GpuStillImageAsset>(new GpuStillImageAsset(
                std::move(path), std::move(host), std::move(*source))), {}};
  }

  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  [[nodiscard]] DigitorRendererBackend backend() const noexcept { return host_.backend; }
  [[nodiscard]] ProcessedGpuFramePtr source_frame() const noexcept { return source_; }

  [[nodiscard]] std::optional<RenderVideoFrame> render_frame(
      std::uint32_t width, std::uint32_t height, std::int64_t timestamp_us,
      std::string* diagnostic = nullptr) const {
    if (!gpu_still_frame_compatible(source_, host_)) {
      if (diagnostic) *diagnostic = "GPU still-image source is unavailable";
      return std::nullopt;
    }
    if (width == 0) width = source_->metadata().width;
    if (height == 0) height = source_->metadata().height;
    ProcessedGpuFramePtr output;
    std::string local;
    const auto result = host_.resize(source_, width, height, timestamp_us, output, local);
    if (result != DIGITOR_RESULT_OK || !gpu_still_frame_compatible(output, host_)) {
      if (local.empty()) local = "GPU resize did not return a compatible GPU frame";
      if (diagnostic) *diagnostic = std::move(local);
      return std::nullopt;
    }
    if (output->metadata().width != width || output->metadata().height != height ||
        output->metadata().timestamp != timestamp_us) {
      if (diagnostic) *diagnostic = "GPU resize metadata mismatch";
      return std::nullopt;
    }
    RenderVideoFrame frame;
    frame.width = width;
    frame.height = height;
    frame.provenance = "gpu-still-image:" + path_;
    frame.gpu = std::move(output);
    return frame;
  }

  ImageIoResult export_image(const ProcessedGpuFramePtr& processed,
                             const std::string& output_path,
                             const ImageExportOptions& options = {}) const {
    if (!gpu_still_frame_compatible(processed, host_)) {
      return {DIGITOR_RESULT_INVALID_ARGUMENT,
              "image export requires a compatible processed GPU frame"};
    }
    if (!host_.encode_image) {
      return {DIGITOR_RESULT_UNSUPPORTED,
              "GPU image encoder or explicit terminal staging callback is unavailable"};
    }
    return host_.encode_image(processed, output_path, options);
  }

 private:
  GpuStillImageAsset(std::string path, GpuStillImageHost host,
                     ProcessedGpuFramePtr source)
      : path_(std::move(path)), host_(std::move(host)), source_(std::move(source)) {}
  std::string path_;
  GpuStillImageHost host_;
  ProcessedGpuFramePtr source_;
};

class GpuStillImageTimelineCache final {
 public:
  explicit GpuStillImageTimelineCache(GpuStillImageHost host)
      : host_(std::move(host)) {}

  ImageIoResult register_clip(const std::string& clip_id, const std::string& path) {
    if (clip_id.empty()) {
      return {DIGITOR_RESULT_INVALID_ARGUMENT, "clip id is empty"};
    }
    auto [asset, result] = GpuStillImageAsset::open(path, host_);
    if (!result) return result;
    std::lock_guard lock(mutex_);
    assets_.insert_or_assign(clip_id, std::move(asset));
    return {};
  }

  bool remove_clip(const std::string& clip_id) noexcept {
    std::lock_guard lock(mutex_);
    return assets_.erase(clip_id) != 0;
  }

  void clear() noexcept {
    std::lock_guard lock(mutex_);
    assets_.clear();
  }

  [[nodiscard]] std::function<std::optional<RenderVideoFrame>(
      const MediaDecodeRequest&)> decoder_callback() {
    return [this](const MediaDecodeRequest& request)
        -> std::optional<RenderVideoFrame> {
      if (!request.require_zero_copy) return std::nullopt;
      std::shared_ptr<GpuStillImageAsset> asset;
      {
        std::lock_guard lock(mutex_);
        const auto found = assets_.find(request.clip_id);
        if (found == assets_.end()) return std::nullopt;
        asset = found->second;
      }
      auto frame = asset->render_frame(request.width, request.height,
                                       request.source_time_us);
      if (!frame || !frame->gpu_resident() || !frame->rgba.empty()) {
        return std::nullopt;
      }
      return frame;
    };
  }

 private:
  GpuStillImageHost host_;
  std::unordered_map<std::string, std::shared_ptr<GpuStillImageAsset>> assets_;
  mutable std::mutex mutex_;
};

}  // namespace digitor
