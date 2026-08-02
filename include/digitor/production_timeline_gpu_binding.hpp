#pragma once

#include "digitor/timeline_media_adapter.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace digitor {

struct ProductionTimelineGpuHost {
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  const void* context_identity{};
  DigitorPixelFormat working_format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
  std::string device_identity;

  std::function<std::optional<ProcessedGpuFramePtr>(std::uint32_t,
                                                     std::uint32_t,
                                                     std::int64_t)> create_target;
  std::function<DigitorResult(const VideoExecutionLayer&,
                              const ProcessedGpuFramePtr&,
                              ProcessedGpuFramePtr&,
                              std::string&)> execute_effects;
  std::function<DigitorResult(const VideoExecutionLayer&,
                              const ProcessedGpuFramePtr&,
                              const ProcessedGpuFramePtr&,
                              ProcessedGpuFramePtr&,
                              std::string&)> composite_layer;
  std::function<bool(const ProcessedGpuFrame&)> frame_evictable;
};

struct ProductionTimelineGpuBindingTelemetry {
  std::uint64_t targets_created{};
  std::uint64_t effect_dispatches{};
  std::uint64_t composite_dispatches{};
  std::uint64_t rejected_cpu_frames{};
  std::uint64_t rejected_backend_frames{};
  std::uint64_t rejected_context_frames{};
  std::uint64_t rejected_metadata_frames{};
  std::string last_diagnostic;
};

class ProductionTimelineGpuBinding final {
 public:
  explicit ProductionTimelineGpuBinding(ProductionTimelineGpuHost host)
      : state_(std::make_shared<State>(std::move(host))) {}

  [[nodiscard]] bool valid() const noexcept {
    const auto& h = state_->host;
    return h.backend != DIGITOR_RENDERER_CPU && h.context_identity != nullptr &&
           h.working_format == DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT &&
           !h.device_identity.empty() && h.create_target && h.execute_effects &&
           h.composite_layer && h.frame_evictable;
  }

  [[nodiscard]] MediaAdapterCallbacks bind(MediaAdapterCallbacks callbacks) const {
    if (!valid()) return {};
    auto state = state_;

    callbacks.create_gpu_target = [state](std::uint32_t width,
                                          std::uint32_t height,
                                          std::int64_t timestamp)
        -> std::optional<RenderVideoFrame> {
      const auto native = state->host.create_target(width, height, timestamp);
      if (!native || !*native ||
          !validate_frame(*state, **native, width, height, timestamp)) {
        state->telemetry.last_diagnostic =
            "production GPU target creation failed validation";
        return std::nullopt;
      }
      ++state->telemetry.targets_created;
      RenderVideoFrame out{};
      out.width = width;
      out.height = height;
      out.provenance = "production-gpu-target:" + state->host.device_identity;
      out.gpu = *native;
      return out;
    };

    callbacks.apply_effects = [state](const VideoExecutionLayer& layer,
                                      RenderVideoFrame& frame) {
      if (!validate_render_frame(*state, frame)) return false;
      ProcessedGpuFramePtr output;
      std::string diagnostic;
      const auto result =
          state->host.execute_effects(layer, frame.gpu, output, diagnostic);
      if (result != DIGITOR_RESULT_OK || !output ||
          !validate_frame(*state, *output, frame.width, frame.height,
                          frame.gpu->metadata().timestamp)) {
        state->telemetry.last_diagnostic = diagnostic.empty()
            ? "native timeline effect dispatch failed"
            : std::move(diagnostic);
        return false;
      }
      frame.gpu = std::move(output);
      frame.provenance =
          "production-gpu-effects:" + state->host.device_identity;
      ++state->telemetry.effect_dispatches;
      return true;
    };

    callbacks.composite = [state](const VideoExecutionLayer& layer,
                                  const RenderVideoFrame& input,
                                  RenderVideoFrame& target) {
      if (!validate_render_frame(*state, input) ||
          !validate_render_frame(*state, target)) {
        return false;
      }
      if (input.width != target.width || input.height != target.height ||
          input.gpu->metadata().timestamp != target.gpu->metadata().timestamp) {
        ++state->telemetry.rejected_metadata_frames;
        state->telemetry.last_diagnostic =
            "timeline composite input/target mismatch";
        return false;
      }
      ProcessedGpuFramePtr output;
      std::string diagnostic;
      const auto result = state->host.composite_layer(
          layer, input.gpu, target.gpu, output, diagnostic);
      if (result != DIGITOR_RESULT_OK || !output ||
          !validate_frame(*state, *output, target.width, target.height,
                          target.gpu->metadata().timestamp)) {
        state->telemetry.last_diagnostic = diagnostic.empty()
            ? "native timeline composite dispatch failed"
            : std::move(diagnostic);
        return false;
      }
      target.gpu = std::move(output);
      target.provenance =
          "production-gpu-composite:" + state->host.device_identity;
      ++state->telemetry.composite_dispatches;
      return true;
    };

    callbacks.gpu_frame_evictable = [state](const ProcessedGpuFrame& frame) {
      return validate_frame(*state, frame, frame.metadata().width,
                            frame.metadata().height,
                            frame.metadata().timestamp) &&
             state->host.frame_evictable(frame);
    };
    return callbacks;
  }

  [[nodiscard]] ProductionTimelineGpuBindingTelemetry telemetry() const {
    return state_->telemetry;
  }

 private:
  struct State {
    explicit State(ProductionTimelineGpuHost value) : host(std::move(value)) {}
    ProductionTimelineGpuHost host;
    ProductionTimelineGpuBindingTelemetry telemetry;
  };

  static bool validate_frame(State& state,
                             const ProcessedGpuFrame& frame,
                             std::uint32_t width,
                             std::uint32_t height,
                             std::int64_t timestamp) {
    if (frame.backend() == DIGITOR_RENDERER_CPU) {
      ++state.telemetry.rejected_cpu_frames;
      state.telemetry.last_diagnostic =
          "CPU frame rejected by production timeline GPU binding";
      return false;
    }
    if (frame.backend() != state.host.backend) {
      ++state.telemetry.rejected_backend_frames;
      state.telemetry.last_diagnostic = "timeline frame backend mismatch";
      return false;
    }
    if (!frame.has_context_identity(state.host.context_identity) ||
        !frame.context_live()) {
      ++state.telemetry.rejected_context_frames;
      state.telemetry.last_diagnostic =
          "timeline frame context/device mismatch or retired context";
      return false;
    }
    const auto& metadata = frame.metadata();
    if (!frame.ready() || metadata.width != width || metadata.height != height ||
        metadata.timestamp != timestamp ||
        metadata.format != state.host.working_format) {
      ++state.telemetry.rejected_metadata_frames;
      state.telemetry.last_diagnostic =
          "timeline frame metadata/readiness mismatch";
      return false;
    }
    return true;
  }

  static bool validate_render_frame(State& state,
                                    const RenderVideoFrame& frame) {
    if (!frame.gpu || !frame.rgba.empty()) {
      ++state.telemetry.rejected_cpu_frames;
      state.telemetry.last_diagnostic =
          "mixed or CPU timeline frame rejected";
      return false;
    }
    return validate_frame(state, *frame.gpu, frame.width, frame.height,
                          frame.gpu->metadata().timestamp);
  }

  std::shared_ptr<State> state_;
};

}  // namespace digitor
