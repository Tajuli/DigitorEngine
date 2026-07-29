#include "digitor/render_policy.hpp"

#include <array>
#include <charconv>
#include <stdexcept>

namespace digitor {
namespace {
void field(std::string& out, std::string_view value) {
  std::array<char, 32> size{};
  const auto [end, error] = std::to_chars(size.data(), size.data() + size.size(), value.size());
  if (error != std::errc{}) throw std::length_error("render identity field too large");
  out.append(size.data(), end).push_back(':');
  out.append(value);
}
void number(std::string& out, std::uint64_t value) {
  std::array<char, 32> text{};
  const auto [end, error] = std::to_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{}) throw std::logic_error("render identity encoding failed");
  field(out, {text.data(), static_cast<std::size_t>(end - text.data())});
}
const MediaSourceDescriptor* find_source(std::span<const MediaSourceDescriptor> sources,
                                         MediaSourceClass source_class) {
  for (const auto& source : sources) if (source.source_class == source_class) return &source;
  return nullptr;
}
void validate(const MediaSourceDescriptor& source) {
  if (source.source_identity.empty() || source.original_media_identity.empty() ||
      source.color_metadata_identity.empty() || !source.width || !source.height)
    throw std::invalid_argument("incomplete media source descriptor");
}
} // namespace

std::vector<std::string> ColorGraphConfiguration::operation_sequence() const {
  std::vector<std::string> result;
  const auto primary = [&] { if (primary_wheels_enabled) result.emplace_back("primary-wheels-v1"); };
  const auto log = [&] { if (log_wheels_enabled) result.emplace_back("log-wheels-v1"); };
  const auto curves = [&] { if (rgb_curves_enabled) result.emplace_back("rgb-curves-v1"); };
  switch (operation_order) {
  case ColorOperationOrder::PrimaryWheelsThenLogWheelsThenRgbCurves: primary(); log(); curves(); break;
  case ColorOperationOrder::PrimaryWheelsThenRgbCurvesThenLogWheels: primary(); curves(); log(); break;
  case ColorOperationOrder::LogWheelsThenPrimaryWheelsThenRgbCurves: log(); primary(); curves(); break;
  case ColorOperationOrder::LogWheelsThenRgbCurvesThenPrimaryWheels: log(); curves(); primary(); break;
  case ColorOperationOrder::RgbCurvesThenPrimaryWheelsThenLogWheels: curves(); primary(); log(); break;
  case ColorOperationOrder::RgbCurvesThenLogWheelsThenPrimaryWheels: curves(); log(); primary(); break;
  }
  return result;
}

std::string ColorGraphConfiguration::identity() const {
  if (color_metadata_identity.empty()) throw std::invalid_argument("missing color metadata identity");
  if (primary_wheels_enabled && primary_wheels_serialization.empty())
    throw std::invalid_argument("missing Primary Wheels serialization");
  if (log_wheels_enabled && log_wheels_serialization.empty())
    throw std::invalid_argument("missing Log Wheels serialization");
  if (rgb_curves_enabled && rgb_curves_serialization.empty())
    throw std::invalid_argument("missing RGB Curves serialization");
  std::string result{"digitor-color-graph:"};
  number(result, schema_version);
  number(result, static_cast<std::uint32_t>(operation_order));
  number(result, static_cast<std::uint32_t>(precision));
  field(result, color_metadata_identity);
  number(result, primary_wheels_enabled); field(result, primary_wheels_serialization);
  number(result, log_wheels_enabled); field(result, log_wheels_serialization);
  number(result, rgb_curves_enabled); field(result, rgb_curves_serialization);
  for (const auto& operation : operation_sequence()) field(result, operation);
  return result;
}

ColorRenderPlan build_color_render_plan(RenderPurpose purpose,
    std::span<const MediaSourceDescriptor> sources,
    const PreviewSourceConfiguration& preview,
    const ColorGraphConfiguration& graph) {
  const auto wanted = purpose == RenderPurpose::Export ? MediaSourceClass::Original : preview.requested_class;
  const auto* selected = find_source(sources, wanted);
  if (!selected) throw std::invalid_argument(purpose == RenderPurpose::Export
      ? "export requires an original full-quality source" : "requested preview source is unavailable");
  validate(*selected);
  if (purpose == RenderPurpose::Export && selected->source_class != MediaSourceClass::Original)
    throw std::invalid_argument("export rejected a non-original source");
  const auto* original = find_source(sources, MediaSourceClass::Original);
  const auto& media_identity = original ? original->original_media_identity : sources.front().original_media_identity;
  if (selected->original_media_identity != media_identity)
    throw std::invalid_argument("source does not belong to the requested original media");
  ColorRenderPlan plan{purpose, *selected, preview, graph, {}};
  std::string key{"digitor-source-cache:"};
  field(key, selected->source_identity); field(key, selected->original_media_identity);
  number(key, selected->width); number(key, selected->height);
  number(key, static_cast<std::uint32_t>(selected->precision)); field(key, selected->color_metadata_identity);
  field(key, graph.identity());
  number(key, purpose == RenderPurpose::Preview ? preview.requested_width : selected->width);
  number(key, purpose == RenderPurpose::Preview ? preview.requested_height : selected->height);
  if (purpose == RenderPurpose::Preview && preview.frame_rate_policy) {
    number(key, preview.frame_rate_policy->numerator); number(key, preview.frame_rate_policy->denominator);
  } else { number(key, 0); number(key, 1); }
  plan.source_cache_identity = std::move(key);
  return plan;
}
} // namespace digitor
