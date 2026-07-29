#pragma once

#include "digitor/gpu_frame.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace digitor {

enum class ColorOperationOrder { PrimaryWheelsThenRgbCurves, RgbCurvesThenPrimaryWheels };

enum class MediaSourceClass : std::uint32_t {
  Original,
  Proxy,
  CompressedPreview,
  DecodeScaled
};
enum class RenderPurpose : std::uint32_t { Preview, Export };
enum class RenderPrecision : std::uint32_t { Float32, Float16 };

struct PreviewFrameRatePolicy {
  std::uint32_t numerator{};
  std::uint32_t denominator{1};
  [[nodiscard]] bool enabled() const noexcept { return numerator != 0; }
};

struct MediaSourceDescriptor {
  MediaSourceClass source_class{MediaSourceClass::Original};
  std::string source_identity;
  std::string original_media_identity;
  std::uint32_t width{};
  std::uint32_t height{};
  RenderPrecision precision{RenderPrecision::Float32};
  std::string color_metadata_identity{"linear-rgba"};
};

struct PreviewSourceConfiguration {
  MediaSourceClass requested_class{MediaSourceClass::Original};
  std::uint32_t requested_width{};
  std::uint32_t requested_height{};
  RenderPrecision precision{RenderPrecision::Float32};
  std::optional<PreviewFrameRatePolicy> frame_rate_policy;
};

struct ColorGraphConfiguration {
  static constexpr std::uint32_t schema_version = 1;
  ColorOperationOrder operation_order{ColorOperationOrder::PrimaryWheelsThenRgbCurves};
  std::string primary_wheels_serialization;
  std::string rgb_curves_serialization;
  RenderPrecision precision{RenderPrecision::Float32};
  std::string color_metadata_identity{"linear-rgba"};
  bool primary_wheels_enabled{};
  bool rgb_curves_enabled{};
  [[nodiscard]] std::string identity() const;
  [[nodiscard]] std::vector<std::string> operation_sequence() const;
};

struct ColorRenderPlan {
  RenderPurpose purpose{RenderPurpose::Preview};
  MediaSourceDescriptor source;
  PreviewSourceConfiguration preview;
  ColorGraphConfiguration graph;
  std::string source_cache_identity;
};

// This is the single source/graph policy path used by preview and export.
// Export never falls back to a non-original source.
[[nodiscard]] ColorRenderPlan build_color_render_plan(
    RenderPurpose purpose, std::span<const MediaSourceDescriptor> sources,
    const PreviewSourceConfiguration& preview,
    const ColorGraphConfiguration& graph);

} // namespace digitor
