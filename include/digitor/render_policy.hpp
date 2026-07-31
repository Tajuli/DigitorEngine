#pragma once

#include "digitor/gpu_frame.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace digitor {

enum class ColorOperationOrder : std::uint32_t {
  PrimaryWheelsThenLogWheelsThenRgbCurves = 0,
  PrimaryWheelsThenRgbCurvesThenLogWheels = 1,
  LogWheelsThenPrimaryWheelsThenRgbCurves = 2,
  LogWheelsThenRgbCurvesThenPrimaryWheels = 3,
  RgbCurvesThenPrimaryWheelsThenLogWheels = 4,
  RgbCurvesThenLogWheelsThenPrimaryWheels = 5,
  // Source compatibility for v4.8 callers. With Log Wheels disabled these aliases
  // preserve the previous two-operation ordering exactly.
  PrimaryWheelsThenRgbCurves = PrimaryWheelsThenLogWheelsThenRgbCurves,
  RgbCurvesThenPrimaryWheels = RgbCurvesThenLogWheelsThenPrimaryWheels
};

enum class MediaSourceClass : std::uint32_t { Original, Proxy, CompressedPreview, DecodeScaled };
enum class RenderPurpose : std::uint32_t { Preview, Export };
enum class RenderPrecision : std::uint32_t { Float32, Float16 };

enum class PreviewQuality : std::uint32_t {
  Full,
  Half,
  Quarter,
  Proxy,
  Adaptive
};

enum class DroppedFramePolicy : std::uint32_t {
  Never,
  DropLateFrames
};

struct AdaptivePreviewConfiguration {
  double target_frame_time_ms{33.333};
  double demote_threshold{1.10};
  double promote_threshold{0.72};
  std::uint32_t stability_window{6};
};

class AdaptivePreviewController {
public:
  explicit AdaptivePreviewController(AdaptivePreviewConfiguration configuration = {});
  void reset() noexcept;
  void observe(double frame_time_ms);
  [[nodiscard]] PreviewQuality effective_quality() const noexcept { return effective_quality_; }
private:
  AdaptivePreviewConfiguration configuration_;
  PreviewQuality effective_quality_{PreviewQuality::Half};
  std::uint32_t fast_samples_{};
  std::uint32_t slow_samples_{};
};

struct RenderDimensions {
  std::uint32_t width{};
  std::uint32_t height{};
};

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
  static constexpr std::uint32_t schema_version = 3;
  ColorOperationOrder operation_order{ColorOperationOrder::PrimaryWheelsThenLogWheelsThenRgbCurves};
  // Preserve the v4.8 aggregate-initialization field order. New v4.9 fields
  // are appended so existing callers and tests remain source-compatible.
  std::string primary_wheels_serialization;
  std::string rgb_curves_serialization;
  RenderPrecision precision{RenderPrecision::Float32};
  std::string color_metadata_identity{"linear-rgba"};
  bool primary_wheels_enabled{};
  bool rgb_curves_enabled{};
  std::string log_wheels_serialization;
  bool log_wheels_enabled{};
  std::string production_node_graph_identity;
  bool production_node_graph_enabled{};
  [[nodiscard]] std::string identity() const;
  [[nodiscard]] std::vector<std::string> operation_sequence() const;
};

struct SourcePixelCoordinate { double x{}; double y{}; };

struct QualifierSampleRequest {
  std::string original_source_identity;
  std::int64_t frame{};
  std::uint32_t source_x{};
  std::uint32_t source_y{};
};

[[nodiscard]] SourcePixelCoordinate map_preview_pixel_to_source(
    double preview_x, double preview_y, const RenderDimensions& preview_dimensions,
    const MediaSourceDescriptor& original_source);

[[nodiscard]] RenderDimensions resolve_preview_dimensions(
    std::uint32_t viewport_width, std::uint32_t viewport_height,
    PreviewQuality quality, const PreviewSourceConfiguration& preview,
    const MediaSourceDescriptor* selected_source = nullptr);

struct ColorRenderPlan {
  RenderPurpose purpose{RenderPurpose::Preview};
  MediaSourceDescriptor source;
  PreviewSourceConfiguration preview;
  ColorGraphConfiguration graph;
  std::string source_cache_identity;
};

[[nodiscard]] ColorRenderPlan build_color_render_plan(
    RenderPurpose purpose, std::span<const MediaSourceDescriptor> sources,
    const PreviewSourceConfiguration& preview,
    const ColorGraphConfiguration& graph);

} // namespace digitor
