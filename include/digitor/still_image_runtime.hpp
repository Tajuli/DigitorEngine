#pragma once

#include "digitor/gpu_image_session.hpp"
#include "digitor/image_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace digitor {

enum class StillImageExecutionBackend : std::uint8_t { gpu, cpu };

struct StillImageCpuHost {
  std::function<DigitorResult(const RenderVideoFrame&, std::uint32_t,
                              std::uint32_t, std::uint64_t, std::uint64_t,
                              RenderVideoFrame&, std::string&)> process;
};

struct StillImageRuntimeOptions {
  bool prefer_gpu{true};
  bool allow_cpu_fallback{true};
  bool validate_parity{false};
  float max_absolute_error{1.0F / 1024.0F};
  double max_rms_error{1.0 / 4096.0};
};

struct StillImageParityReport {
  bool compared{};
  bool equivalent{};
  std::uint64_t compared_components{};
  std::uint64_t failing_components{};
  float max_absolute_error{};
  double rms_error{};
  std::string diagnostic;
};

struct StillImageRuntimeSnapshot {
  StillImageExecutionBackend backend{StillImageExecutionBackend::cpu};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t graph_revision{};
  std::uint64_t parameter_revision{};
  bool open{};
};

class StillImageRuntime final {
 public:
  using OpenResult =
      std::pair<std::unique_ptr<StillImageRuntime>, ImageIoResult>;

  static OpenResult open(
      std::string path, GpuImageSessionHost gpu_host,
      StillImageCpuHost cpu_host, StillImageRuntimeOptions options = {}) {
    if (path.empty()) {
      return OpenResult{
          std::unique_ptr<StillImageRuntime>{},
          ImageIoResult{DIGITOR_RESULT_INVALID_ARGUMENT,
                        "image path is empty"}};
    }

    const bool gpu_selected = gpu_image_session_host_valid(gpu_host);
    if (gpu_selected) {
      auto [gpu, result] = GpuImageSession::open(path, std::move(gpu_host));
      if (!result || !gpu) {
        return OpenResult{std::unique_ptr<StillImageRuntime>{},
                          std::move(result)};
      }
      return OpenResult{
          std::unique_ptr<StillImageRuntime>(new StillImageRuntime(
              std::move(path), std::move(gpu), nullptr,
              std::move(cpu_host), options)),
          ImageIoResult{}};
    }

    if (!options.allow_cpu_fallback) {
      return OpenResult{
          std::unique_ptr<StillImageRuntime>{},
          ImageIoResult{DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                        "no usable GPU backend and CPU fallback is disabled"}};
    }
    if (!cpu_host.process) {
      return OpenResult{
          std::unique_ptr<StillImageRuntime>{},
          ImageIoResult{DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                        "CPU still-image processing host is unavailable"}};
    }

    auto [cpu, result] = StillImageAsset::open(path);
    if (!result || !cpu) {
      return OpenResult{std::unique_ptr<StillImageRuntime>{},
                        std::move(result)};
    }
    return OpenResult{
        std::unique_ptr<StillImageRuntime>(new StillImageRuntime(
            std::move(path), nullptr, std::move(cpu),
            std::move(cpu_host), options)),
        ImageIoResult{}};
  }

  StillImageRuntime(const StillImageRuntime&) = delete;
  StillImageRuntime& operator=(const StillImageRuntime&) = delete;

  [[nodiscard]] StillImageRuntimeSnapshot snapshot() const {
    StillImageRuntimeSnapshot out;
    out.backend = gpu_ ? StillImageExecutionBackend::gpu
                       : StillImageExecutionBackend::cpu;
    out.graph_revision = graph_revision_;
    out.parameter_revision = parameter_revision_;
    if (gpu_) {
      const auto state = gpu_->snapshot();
      out.width = state.source_width;
      out.height = state.source_height;
      out.open = state.open;
    } else if (cpu_) {
      out.width = cpu_->width();
      out.height = cpu_->height();
      out.open = true;
    }
    return out;
  }

  void set_graph_revision(std::uint64_t revision) noexcept {
    graph_revision_ = revision;
    if (gpu_) gpu_->set_graph_revision(revision);
  }

  void set_parameter_revision(std::uint64_t revision) noexcept {
    parameter_revision_ = revision;
    if (gpu_) gpu_->set_parameter_revision(revision);
  }

  [[nodiscard]] std::optional<RenderVideoFrame> render_cpu(
      std::uint32_t width = 0, std::uint32_t height = 0,
      std::string* diagnostic = nullptr) const {
    if (!cpu_ || !cpu_host_.process) {
      if (diagnostic) *diagnostic = "CPU still-image runtime is unavailable";
      return std::nullopt;
    }
    auto source = cpu_->render_frame(width, height);
    if (!source || !source->valid()) {
      if (diagnostic) *diagnostic = "CPU image decode/resize failed";
      return std::nullopt;
    }
    RenderVideoFrame output;
    std::string local;
    const auto result = cpu_host_.process(
        *source, source->width, source->height, graph_revision_,
        parameter_revision_, output, local);
    if (result != DIGITOR_RESULT_OK || !output.valid() ||
        output.gpu_resident()) {
      if (diagnostic) {
        *diagnostic = local.empty() ? "CPU node/effect processing failed"
                                    : std::move(local);
      }
      return std::nullopt;
    }
    return output;
  }

  ImageIoResult export_image(const std::string& output_path,
                             const ImageExportOptions& options = {},
                             std::string* diagnostic = nullptr) {
    if (gpu_) return gpu_->export_image(output_path, options, diagnostic);
    auto frame = render_cpu(options.width, options.height, diagnostic);
    if (!frame) {
      return {DIGITOR_RESULT_INTERNAL_ERROR,
              diagnostic && !diagnostic->empty()
                  ? *diagnostic
                  : "CPU image render failed"};
    }
    return export_image_frame(*frame, output_path, options);
  }

  static StillImageParityReport compare_cpu_frames(
      const RenderVideoFrame& preview, const RenderVideoFrame& output,
      const StillImageRuntimeOptions& options = {}) {
    StillImageParityReport report;
    report.compared = true;
    if (!preview.valid() || !output.valid() || preview.gpu_resident() ||
        output.gpu_resident() || preview.width != output.width ||
        preview.height != output.height ||
        preview.rgba.size() != output.rgba.size()) {
      report.diagnostic = "frames are not comparable CPU RGBA frames";
      return report;
    }
    long double squared = 0.0L;
    report.compared_components = preview.rgba.size();
    for (std::size_t i = 0; i < preview.rgba.size(); ++i) {
      const auto error = std::fabs(preview.rgba[i] - output.rgba[i]);
      report.max_absolute_error =
          std::max(report.max_absolute_error, error);
      squared += static_cast<long double>(error) * error;
      if (error > options.max_absolute_error) ++report.failing_components;
    }
    if (report.compared_components != 0) {
      report.rms_error = std::sqrt(static_cast<double>(
          squared / static_cast<long double>(report.compared_components)));
    }
    report.equivalent = report.failing_components == 0 &&
                        report.rms_error <= options.max_rms_error;
    if (!report.equivalent) {
      report.diagnostic = "per-pixel parity threshold exceeded";
    }
    return report;
  }

 private:
  StillImageRuntime(std::string path, std::unique_ptr<GpuImageSession> gpu,
                    std::shared_ptr<StillImageAsset> cpu,
                    StillImageCpuHost cpu_host,
                    StillImageRuntimeOptions options)
      : path_(std::move(path)),
        gpu_(std::move(gpu)),
        cpu_(std::move(cpu)),
        cpu_host_(std::move(cpu_host)),
        options_(options) {}

  std::string path_;
  std::unique_ptr<GpuImageSession> gpu_;
  std::shared_ptr<StillImageAsset> cpu_;
  StillImageCpuHost cpu_host_;
  StillImageRuntimeOptions options_;
  std::uint64_t graph_revision_{};
  std::uint64_t parameter_revision_{};
};

}  // namespace digitor
