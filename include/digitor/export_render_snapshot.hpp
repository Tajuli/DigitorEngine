#pragma once

#include "digitor/digitor.h"
#include "digitor/gpu_frame.hpp"
#include "digitor/production_export.hpp"

#include <cstdint>
#include <string>

namespace digitor {

enum class ExportExecutionPolicy : std::uint32_t {
  hardware_required = 1,
  explicit_cpu_reference = 2,
  unsupported = 3,
};

enum class ExportAlphaPolicy : std::uint32_t {
  discard = 1,
  straight = 2,
  premultiplied = 3,
};

struct ExportRenderSnapshot final {
  std::uint64_t snapshot_identity{};
  std::uint64_t timeline_revision{};
  std::uint64_t render_revision{};
  std::uint64_t node_graph_revision{};
  std::uint64_t color_pipeline_revision{};
  std::uint64_t audio_revision{};

  std::uint32_t width{};
  std::uint32_t height{};
  DigitorPixelFormat working_format{DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
  ExportAlphaPolicy alpha_policy{ExportAlphaPolicy::discard};

  std::int32_t fps_num{};
  std::int32_t fps_den{};
  std::int64_t duration_us{};
  bool variable_frame_rate{};
  bool hdr{};
  std::string color_metadata;

  ExportProfile profile;
  ExportExecutionPolicy policy{ExportExecutionPolicy::unsupported};
  DigitorRendererBackend renderer_backend{DIGITOR_RENDERER_CPU};
  EncoderBackend encoder_backend{EncoderBackend::software};
};

struct ExportContractValidation final {
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  const char* diagnostic{"invalid export snapshot"};
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] inline bool export_policy_uses_gpu(
    ExportExecutionPolicy policy) noexcept {
  return policy == ExportExecutionPolicy::hardware_required;
}

[[nodiscard]] inline ExportContractValidation validate_export_snapshot(
    const ExportRenderSnapshot& value) noexcept {
  if (value.snapshot_identity == 0 || value.timeline_revision == 0 ||
      value.render_revision == 0 || value.node_graph_revision == 0 ||
      value.color_pipeline_revision == 0 || value.audio_revision == 0) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "export snapshot revisions must be frozen and non-zero"};
  }
  if (value.width == 0 || value.height == 0 || value.fps_num <= 0 ||
      value.fps_den <= 0 || value.duration_us <= 0) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "invalid export dimensions, timing, or duration"};
  }
  if (value.profile.width != static_cast<int>(value.width) ||
      value.profile.height != static_cast<int>(value.height) ||
      value.profile.fps_num != value.fps_num ||
      value.profile.fps_den != value.fps_den) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "export profile does not match frozen render snapshot"};
  }
  if (value.color_metadata.empty()) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "export color metadata is required"};
  }
  if (value.policy == ExportExecutionPolicy::hardware_required) {
    if (value.renderer_backend == DIGITOR_RENDERER_CPU) {
      return {DIGITOR_RESULT_UNSUPPORTED, "hardware-required export cannot use the CPU renderer"};
    }
    if (value.encoder_backend == EncoderBackend::software) {
      return {DIGITOR_RESULT_UNSUPPORTED, "hardware-required export cannot use a software encoder"};
    }
  } else if (value.policy == ExportExecutionPolicy::explicit_cpu_reference) {
    if (value.renderer_backend != DIGITOR_RENDERER_CPU ||
        value.encoder_backend != EncoderBackend::software) {
      return {DIGITOR_RESULT_UNSUPPORTED, "CPU/reference export must be an explicit all-CPU job"};
    }
  } else {
    return {DIGITOR_RESULT_UNSUPPORTED, "export combination is unsupported"};
  }
  return {DIGITOR_RESULT_OK, "ok"};
}

[[nodiscard]] inline ExportContractValidation validate_frame_against_snapshot(
    const ExportRenderSnapshot& snapshot,
    const ProcessedGpuFrame& frame) noexcept {
  const auto snapshot_result = validate_export_snapshot(snapshot);
  if (!snapshot_result) return snapshot_result;
  if (!frame.ready() || !frame.context_live()) {
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE, "GPU frame is not ready or its context is retired"};
  }
  const auto& metadata = frame.metadata();
  if (metadata.width != snapshot.width || metadata.height != snapshot.height ||
      metadata.format != snapshot.working_format) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "GPU frame format or dimensions differ from frozen snapshot"};
  }
  if (metadata.color_metadata != snapshot.color_metadata) {
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "GPU frame color metadata differs from frozen snapshot"};
  }
  if (snapshot.policy == ExportExecutionPolicy::hardware_required &&
      frame.backend() == DIGITOR_RENDERER_CPU) {
    return {DIGITOR_RESULT_UNSUPPORTED, "hardware export rejected a CPU frame"};
  }
  return {DIGITOR_RESULT_OK, "ok"};
}

}  // namespace digitor
