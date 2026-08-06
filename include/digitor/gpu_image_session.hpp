#pragma once

#include "digitor/gpu_image_io.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace digitor {

// GPU-only processing callback shared by photo preview and photo export.
// Implementations must run the same node/color/effect graph, shader artifacts,
// parameter buffers, precision, alpha convention and color transforms for both
// modes. The callback must never return a CPU frame or silently fall back to CPU.
enum class GpuImageSessionRenderMode : std::uint8_t { preview, export_render };

struct GpuImageSessionProcessRequest {
  GpuImageSessionRenderMode mode{GpuImageSessionRenderMode::preview};
  ProcessedGpuFramePtr source;
  std::uint32_t width{};
  std::uint32_t height{};
  std::int64_t timestamp_us{};
  std::uint64_t graph_revision{};
  std::uint64_t parameter_revision{};
};

struct GpuImageSessionHost {
  GpuStillImageHost image_io;
  std::function<DigitorResult(const GpuImageSessionProcessRequest&,
                              ProcessedGpuFramePtr&, std::string&)> process;
};

inline bool gpu_image_session_host_valid(const GpuImageSessionHost& host) noexcept {
  return gpu_still_host_valid(host.image_io) && static_cast<bool>(host.process);
}

struct GpuImageSessionSnapshot {
  std::string source_path;
  std::uint32_t source_width{};
  std::uint32_t source_height{};
  std::uint64_t graph_revision{};
  std::uint64_t parameter_revision{};
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  bool open{};
};

class GpuImageSession final {
 public:
  static std::pair<std::unique_ptr<GpuImageSession>, ImageIoResult> open(
      std::string path, GpuImageSessionHost host) {
    if (!gpu_image_session_host_valid(host)) {
      return {nullptr, {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                        "GPU image-session host is incomplete"}};
    }
    auto [asset, result] = GpuStillImageAsset::open(std::move(path), host.image_io);
    if (!result) return {nullptr, std::move(result)};
    return {std::unique_ptr<GpuImageSession>(
                new GpuImageSession(std::move(asset), std::move(host))), {}};
  }

  GpuImageSession(const GpuImageSession&) = delete;
  GpuImageSession& operator=(const GpuImageSession&) = delete;

  [[nodiscard]] GpuImageSessionSnapshot snapshot() const {
    std::lock_guard lock(mutex_);
    GpuImageSessionSnapshot value;
    value.source_path = asset_ ? asset_->path() : std::string{};
    if (asset_ && asset_->source_frame()) {
      value.source_width = asset_->source_frame()->metadata().width;
      value.source_height = asset_->source_frame()->metadata().height;
      value.backend = asset_->backend();
      value.open = true;
    }
    value.graph_revision = graph_revision_;
    value.parameter_revision = parameter_revision_;
    return value;
  }

  void set_graph_revision(std::uint64_t revision) noexcept {
    std::lock_guard lock(mutex_);
    graph_revision_ = revision;
    invalidate_locked();
  }

  void set_parameter_revision(std::uint64_t revision) noexcept {
    std::lock_guard lock(mutex_);
    parameter_revision_ = revision;
    invalidate_locked();
  }

  [[nodiscard]] std::optional<ProcessedGpuFramePtr> render_preview(
      std::uint32_t width, std::uint32_t height, std::int64_t timestamp_us = 0,
      std::string* diagnostic = nullptr) {
    return render(GpuImageSessionRenderMode::preview, width, height, timestamp_us,
                  diagnostic);
  }

  [[nodiscard]] std::optional<ProcessedGpuFramePtr> render_export(
      std::uint32_t width, std::uint32_t height, std::int64_t timestamp_us = 0,
      std::string* diagnostic = nullptr) {
    return render(GpuImageSessionRenderMode::export_render, width, height,
                  timestamp_us, diagnostic);
  }

  ImageIoResult export_image(const std::string& output_path,
                             const ImageExportOptions& options = {},
                             std::string* render_diagnostic = nullptr) {
    const auto state = snapshot();
    const auto width = options.width == 0 ? state.source_width : options.width;
    const auto height = options.height == 0 ? state.source_height : options.height;
    auto processed = render_export(width, height, 0, render_diagnostic);
    if (!processed || !*processed) {
      return {DIGITOR_RESULT_INTERNAL_ERROR,
              render_diagnostic && !render_diagnostic->empty()
                  ? *render_diagnostic
                  : "GPU image export render failed"};
    }
    return asset_->export_image(*processed, output_path, options);
  }

  [[nodiscard]] bool preview_export_equivalent(
      std::uint32_t width, std::uint32_t height, std::int64_t timestamp_us = 0,
      std::string* diagnostic = nullptr) {
    std::string preview_diagnostic;
    std::string export_diagnostic;
    auto preview = render_preview(width, height, timestamp_us, &preview_diagnostic);
    auto output = render_export(width, height, timestamp_us, &export_diagnostic);
    if (!preview || !output || !*preview || !*output) {
      if (diagnostic) {
        *diagnostic = !preview_diagnostic.empty() ? preview_diagnostic
                                                  : export_diagnostic;
      }
      return false;
    }
    const auto& a = (*preview)->metadata();
    const auto& b = (*output)->metadata();
    const bool equivalent = (*preview)->backend() == (*output)->backend() &&
                            a.width == b.width && a.height == b.height &&
                            a.timestamp == b.timestamp &&
                            (*preview)->has_context_identity(host_.image_io.context_identity) &&
                            (*output)->has_context_identity(host_.image_io.context_identity);
    if (!equivalent && diagnostic) {
      *diagnostic = "preview/export GPU frame metadata or context mismatch";
    }
    return equivalent;
  }

 private:
  GpuImageSession(std::shared_ptr<GpuStillImageAsset> asset,
                  GpuImageSessionHost host)
      : asset_(std::move(asset)), host_(std::move(host)) {}

  struct CachedFrame {
    GpuImageSessionRenderMode mode{GpuImageSessionRenderMode::preview};
    std::uint32_t width{};
    std::uint32_t height{};
    std::int64_t timestamp_us{};
    std::uint64_t graph_revision{};
    std::uint64_t parameter_revision{};
    ProcessedGpuFramePtr frame;
  };

  [[nodiscard]] std::optional<ProcessedGpuFramePtr> render(
      GpuImageSessionRenderMode mode, std::uint32_t width, std::uint32_t height,
      std::int64_t timestamp_us, std::string* diagnostic) {
    std::lock_guard lock(mutex_);
    if (!asset_ || !asset_->source_frame()) {
      if (diagnostic) *diagnostic = "GPU image session is closed";
      return std::nullopt;
    }
    if (width == 0) width = asset_->source_frame()->metadata().width;
    if (height == 0) height = asset_->source_frame()->metadata().height;
    if (cache_ && cache_->mode == mode && cache_->width == width &&
        cache_->height == height && cache_->timestamp_us == timestamp_us &&
        cache_->graph_revision == graph_revision_ &&
        cache_->parameter_revision == parameter_revision_ &&
        gpu_still_frame_compatible(cache_->frame, host_.image_io)) {
      return cache_->frame;
    }

    std::string local;
    auto resized = asset_->render_frame(width, height, timestamp_us, &local);
    if (!resized || !resized->gpu_resident() || !resized->rgba.empty()) {
      if (diagnostic) {
        *diagnostic = local.empty() ? "GPU image resize failed" : std::move(local);
      }
      return std::nullopt;
    }

    GpuImageSessionProcessRequest request;
    request.mode = mode;
    request.source = resized->gpu;
    request.width = width;
    request.height = height;
    request.timestamp_us = timestamp_us;
    request.graph_revision = graph_revision_;
    request.parameter_revision = parameter_revision_;

    ProcessedGpuFramePtr output;
    local.clear();
    const auto result = host_.process(request, output, local);
    if (result != DIGITOR_RESULT_OK ||
        !gpu_still_frame_compatible(output, host_.image_io) ||
        output->metadata().width != width ||
        output->metadata().height != height ||
        output->metadata().timestamp != timestamp_us) {
      if (diagnostic) {
        *diagnostic = local.empty()
                          ? "GPU image processing returned an incompatible frame"
                          : std::move(local);
      }
      return std::nullopt;
    }

    cache_ = CachedFrame{mode, width, height, timestamp_us, graph_revision_,
                         parameter_revision_, output};
    return output;
  }

  void invalidate_locked() noexcept { cache_.reset(); }

  std::shared_ptr<GpuStillImageAsset> asset_;
  GpuImageSessionHost host_;
  mutable std::mutex mutex_;
  std::uint64_t graph_revision_{};
  std::uint64_t parameter_revision_{};
  std::optional<CachedFrame> cache_;
};

}  // namespace digitor
