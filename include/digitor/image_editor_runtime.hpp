#pragma once

#include "digitor/gpu_image_session.hpp"
#include "digitor/image_io.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace digitor {

enum class ImageEditorExecutionBackend : std::uint8_t { gpu, cpu };

struct ImageEditorCpuProcessRequest {
  RenderVideoFrame source;
  std::uint32_t width{};
  std::uint32_t height{};
  std::int64_t timestamp_us{};
  std::uint64_t graph_revision{};
  std::uint64_t parameter_revision{};
};

struct ImageEditorRuntimeConfig {
  GpuImageSessionHost gpu_host;
  std::function<DigitorResult(const ImageEditorCpuProcessRequest&,
                              RenderVideoFrame&, std::string&)>
      process_cpu;
  bool allow_cpu_fallback{true};
};

struct ImageEditorPreviewFrame {
  ImageEditorExecutionBackend backend{ImageEditorExecutionBackend::cpu};
  ProcessedGpuFramePtr gpu;
  std::optional<RenderVideoFrame> cpu;

  [[nodiscard]] bool valid() const noexcept {
    return backend == ImageEditorExecutionBackend::gpu
               ? static_cast<bool>(gpu)
               : cpu.has_value() && cpu->valid() && !cpu->gpu_resident();
  }
};

struct ImageEditorRuntimeSnapshot {
  ImageEditorExecutionBackend backend{ImageEditorExecutionBackend::cpu};
  std::uint32_t source_width{};
  std::uint32_t source_height{};
  std::uint64_t graph_revision{};
  std::uint64_t parameter_revision{};
  bool open{};
};

class ImageEditorRuntime final {
 public:
  using OpenResult =
      std::pair<std::unique_ptr<ImageEditorRuntime>, ImageIoResult>;

  static OpenResult open(std::string path, ImageEditorRuntimeConfig config) {
    if (path.empty()) {
      return OpenResult{std::unique_ptr<ImageEditorRuntime>{},
                        ImageIoResult{DIGITOR_RESULT_INVALID_ARGUMENT,
                                      "image path is empty"}};
    }

    if (gpu_image_session_host_valid(config.gpu_host)) {
      auto [session, result] =
          GpuImageSession::open(path, std::move(config.gpu_host));
      if (!result || !session) {
        // A usable GPU host selects and locks GPU for the session. GPU open,
        // decode or upload failures never switch to CPU.
        return OpenResult{std::unique_ptr<ImageEditorRuntime>{},
                          std::move(result)};
      }
      return OpenResult{
          std::unique_ptr<ImageEditorRuntime>(new ImageEditorRuntime(
              std::move(path), std::move(session), nullptr,
              std::move(config.process_cpu))),
          ImageIoResult{}};
    }

    if (!config.allow_cpu_fallback) {
      return OpenResult{
          std::unique_ptr<ImageEditorRuntime>{},
          ImageIoResult{DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                        "no usable GPU host and CPU fallback is disabled"}};
    }
    if (!config.process_cpu) {
      return OpenResult{
          std::unique_ptr<ImageEditorRuntime>{},
          ImageIoResult{DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                        "CPU image processing host is unavailable"}};
    }

    auto [asset, result] = StillImageAsset::open(path);
    if (!result || !asset) {
      return OpenResult{std::unique_ptr<ImageEditorRuntime>{},
                        std::move(result)};
    }
    return OpenResult{
        std::unique_ptr<ImageEditorRuntime>(new ImageEditorRuntime(
            std::move(path), nullptr, std::move(asset),
            std::move(config.process_cpu))),
        ImageIoResult{}};
  }

  ImageEditorRuntime(const ImageEditorRuntime&) = delete;
  ImageEditorRuntime& operator=(const ImageEditorRuntime&) = delete;

  [[nodiscard]] ImageEditorRuntimeSnapshot snapshot() const {
    std::lock_guard lock(mutex_);
    ImageEditorRuntimeSnapshot value;
    value.backend = gpu_ ? ImageEditorExecutionBackend::gpu
                         : ImageEditorExecutionBackend::cpu;
    value.graph_revision = graph_revision_;
    value.parameter_revision = parameter_revision_;
    if (gpu_) {
      const auto state = gpu_->snapshot();
      value.source_width = state.source_width;
      value.source_height = state.source_height;
      value.open = state.open;
    } else if (cpu_) {
      value.source_width = cpu_->width();
      value.source_height = cpu_->height();
      value.open = true;
    }
    return value;
  }

  void set_graph_revision(std::uint64_t revision) noexcept {
    std::lock_guard lock(mutex_);
    graph_revision_ = revision;
    cpu_cache_.reset();
    if (gpu_) gpu_->set_graph_revision(revision);
  }

  void set_parameter_revision(std::uint64_t revision) noexcept {
    std::lock_guard lock(mutex_);
    parameter_revision_ = revision;
    cpu_cache_.reset();
    if (gpu_) gpu_->set_parameter_revision(revision);
  }

  [[nodiscard]] std::optional<ImageEditorPreviewFrame> render_preview(
      std::uint32_t width = 0, std::uint32_t height = 0,
      std::int64_t timestamp_us = 0, std::string* diagnostic = nullptr) {
    std::lock_guard lock(mutex_);
    if (gpu_) {
      auto frame = gpu_->render_preview(width, height, timestamp_us, diagnostic);
      if (!frame || !*frame) return std::nullopt;
      return ImageEditorPreviewFrame{ImageEditorExecutionBackend::gpu,
                                     *frame, std::nullopt};
    }
    if (!cpu_ || !process_cpu_) {
      if (diagnostic) *diagnostic = "CPU image editor runtime is unavailable";
      return std::nullopt;
    }

    if (width == 0) width = cpu_->width();
    if (height == 0) height = cpu_->height();
    if (cpu_cache_ && cpu_cache_->width == width &&
        cpu_cache_->height == height &&
        cpu_cache_->timestamp_us == timestamp_us &&
        cpu_cache_->graph_revision == graph_revision_ &&
        cpu_cache_->parameter_revision == parameter_revision_) {
      return ImageEditorPreviewFrame{ImageEditorExecutionBackend::cpu,
                                     nullptr, cpu_cache_->frame};
    }

    auto source = cpu_->render_frame(width, height);
    if (!source || !source->valid() || source->gpu_resident()) {
      if (diagnostic) *diagnostic = "CPU image decode or resize failed";
      return std::nullopt;
    }

    ImageEditorCpuProcessRequest request;
    request.source = *source;
    request.width = width;
    request.height = height;
    request.timestamp_us = timestamp_us;
    request.graph_revision = graph_revision_;
    request.parameter_revision = parameter_revision_;

    RenderVideoFrame output;
    std::string local;
    const auto result = process_cpu_(request, output, local);
    if (result != DIGITOR_RESULT_OK || !output.valid() ||
        output.gpu_resident() || output.width != width ||
        output.height != height) {
      if (diagnostic) {
        *diagnostic = local.empty() ? "CPU node/effect processing failed"
                                    : std::move(local);
      }
      return std::nullopt;
    }
    cpu_cache_ = CpuCache{width, height, timestamp_us, graph_revision_,
                          parameter_revision_, output};
    return ImageEditorPreviewFrame{ImageEditorExecutionBackend::cpu,
                                   nullptr, std::move(output)};
  }

  ImageIoResult export_image(const std::string& output_path,
                             const ImageExportOptions& options = {},
                             std::string* diagnostic = nullptr) {
    if (gpu_) return gpu_->export_image(output_path, options, diagnostic);
    auto preview = render_preview(options.width, options.height, 0, diagnostic);
    if (!preview || !preview->cpu) {
      return ImageIoResult{
          DIGITOR_RESULT_INTERNAL_ERROR,
          diagnostic && !diagnostic->empty() ? *diagnostic
                                              : "CPU image export render failed"};
    }
    ImageExportOptions direct = options;
    direct.width = 0;
    direct.height = 0;
    return export_image_frame(*preview->cpu, output_path, direct);
  }

 private:
  struct CpuCache {
    std::uint32_t width{};
    std::uint32_t height{};
    std::int64_t timestamp_us{};
    std::uint64_t graph_revision{};
    std::uint64_t parameter_revision{};
    RenderVideoFrame frame;
  };

  ImageEditorRuntime(
      std::string path, std::unique_ptr<GpuImageSession> gpu,
      std::shared_ptr<StillImageAsset> cpu,
      std::function<DigitorResult(const ImageEditorCpuProcessRequest&,
                                  RenderVideoFrame&, std::string&)>
          process_cpu)
      : path_(std::move(path)),
        gpu_(std::move(gpu)),
        cpu_(std::move(cpu)),
        process_cpu_(std::move(process_cpu)) {}

  std::string path_;
  std::unique_ptr<GpuImageSession> gpu_;
  std::shared_ptr<StillImageAsset> cpu_;
  std::function<DigitorResult(const ImageEditorCpuProcessRequest&,
                              RenderVideoFrame&, std::string&)>
      process_cpu_;
  mutable std::mutex mutex_;
  std::uint64_t graph_revision_{};
  std::uint64_t parameter_revision_{};
  std::optional<CpuCache> cpu_cache_;
};

}  // namespace digitor
