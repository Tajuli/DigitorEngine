#include "digitor/hdr_ecosystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <utility>

namespace digitor {
namespace {
void set_diag(std::string* out, std::string value) {
  if (out) *out = std::move(value);
}
bool finite_color(const Color& c) {
  return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a);
}
double luminance(const Color& c) {
  return std::max(0.0, 0.2627 * static_cast<double>(c.r) +
                         0.6780 * static_cast<double>(c.g) +
                         0.0593 * static_cast<double>(c.b));
}
double mapped(double nits, const AdvancedToneMapConfig& c) {
  if (c.operation == HdrToneMapOperator::none || c.source_peak_nits <= c.target_peak_nits)
    return nits;
  const double x = std::max(0.0, nits) / c.source_peak_nits;
  const double ratio = c.target_peak_nits / c.source_peak_nits;
  double y = x;
  switch (c.operation) {
    case HdrToneMapOperator::bt2390:
      y = x <= ratio ? x : ratio + (1.0 - ratio) * (1.0 - std::exp(-(x - ratio) / std::max(1e-6, c.highlight_rolloff)));
      break;
    case HdrToneMapOperator::perceptual:
      y = x / (1.0 + x * (1.0 / std::max(ratio, 1e-6) - 1.0));
      break;
    case HdrToneMapOperator::scene_adaptive:
      y = std::pow(x / (1.0 + x), 0.9) * ratio;
      break;
    case HdrToneMapOperator::display_adaptive:
      y = std::log1p(9.0 * x) / std::log(10.0) * ratio;
      break;
    case HdrToneMapOperator::none: break;
  }
  return std::clamp(y * c.source_peak_nits, 0.0, c.target_peak_nits);
}
void append_u32(std::vector<std::byte>& out, std::uint32_t value) {
  for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::byte>((value >> s) & 0xffu));
}
}

HdrEcosystem::HdrEcosystem(HdrAdapterCallbacks callbacks) : callbacks_(std::move(callbacks)) {}

DigitorResult HdrEcosystem::validate(const HdrFrameMetadata& metadata,
                                     std::string* diagnostic) const {
  if (metadata.standard == HdrStandard::hdr10_plus && !metadata.hdr10_plus) {
    set_diag(diagnostic, "HDR10+ requires dynamic metadata");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (metadata.standard == HdrStandard::dolby_vision && !metadata.dolby_vision) {
    set_diag(diagnostic, "Dolby Vision requires metadata");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (metadata.hdr10_plus) {
    if (metadata.hdr10_plus->windows.empty() || metadata.hdr10_plus->windows.size() > 3u) {
      set_diag(diagnostic, "HDR10+ requires one to three processing windows");
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    for (const auto& w : metadata.hdr10_plus->windows) {
      if (w.distribution_percentiles.size() != w.distribution_values.size() ||
          w.bezier_anchors.size() > 15u || w.knee_x < 0.0 || w.knee_x > 1.0 ||
          w.knee_y < 0.0 || w.knee_y > 1.0) {
        set_diag(diagnostic, "invalid HDR10+ window metadata");
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
    }
  }
  if (metadata.dolby_vision) {
    const auto& d = *metadata.dolby_vision;
    if (d.profile < 5 || d.profile > 9 || d.level < 0 || d.level > 13) {
      set_diag(diagnostic, "unsupported Dolby Vision profile or level");
      return DIGITOR_RESULT_UNSUPPORTED;
    }
  }
  set_diag(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult HdrEcosystem::analyze(std::span<const Color> pixels,
                                    double reference_white_nits,
                                    HdrAnalysisResult& result,
                                    std::string* diagnostic) {
  if (pixels.empty() || !(reference_white_nits > 0.0) || !std::isfinite(reference_white_nits)) {
    set_diag(diagnostic, "HDR analysis requires pixels and a finite positive reference white");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  std::vector<double> values;
  values.reserve(pixels.size());
  double sum = 0.0;
  for (const auto& pixel : pixels) {
    if (!finite_color(pixel)) {
      set_diag(diagnostic, "HDR analysis rejected a non-finite pixel");
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    const double nits = luminance(pixel) * reference_white_nits;
    values.push_back(nits);
    sum += nits;
  }
  std::sort(values.begin(), values.end());
  result = {};
  result.max_rgb_nits = values.back();
  result.average_rgb_nits = sum / static_cast<double>(values.size());
  result.max_cll = result.max_rgb_nits;
  result.max_fall = result.average_rgb_nits;
  static constexpr double percentiles[] = {0.01, 0.1, 0.25, 0.5, 0.75, 0.9, 0.99};
  for (double p : percentiles) {
    const std::size_t index = std::min(values.size() - 1u,
        static_cast<std::size_t>(p * static_cast<double>(values.size() - 1u)));
    result.percentile_nits.push_back(values[index]);
  }
  ++telemetry_.analyzed_frames;
  set_diag(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult HdrEcosystem::tone_map(std::span<const Color> source,
                                     std::span<Color> destination,
                                     const AdvancedToneMapConfig& config,
                                     std::string* diagnostic) {
  if (source.empty() || source.size() != destination.size() ||
      !(config.source_peak_nits > 0.0) || !(config.target_peak_nits > 0.0) ||
      !(config.reference_white_nits > 0.0)) {
    set_diag(diagnostic, "invalid tone-map buffers or luminance configuration");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  for (std::size_t i = 0; i < source.size(); ++i) {
    if (!finite_color(source[i])) {
      set_diag(diagnostic, "tone map rejected a non-finite pixel");
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    const double in_luma = std::max(1e-9, luminance(source[i]));
    const double out_luma = mapped(in_luma * config.reference_white_nits, config) /
                            config.reference_white_nits;
    const double scale = out_luma / in_luma;
    Color out{static_cast<float>(source[i].r * scale),
              static_cast<float>(source[i].g * scale),
              static_cast<float>(source[i].b * scale), source[i].a};
    const double gray = luminance(out);
    out.r = static_cast<float>(gray + (out.r - gray) * config.saturation_compensation);
    out.g = static_cast<float>(gray + (out.g - gray) * config.saturation_compensation);
    out.b = static_cast<float>(gray + (out.b - gray) * config.saturation_compensation);
    destination[i] = out;
  }
  telemetry_.tone_mapped_pixels += source.size();
  set_diag(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult HdrEcosystem::generate_hdr10_plus(std::uint64_t frame_index,
                                                std::int64_t timestamp_us,
                                                const HdrAnalysisResult& analysis,
                                                bool scene_refresh,
                                                Hdr10PlusMetadata& metadata,
                                                std::string* diagnostic) {
  if (analysis.percentile_nits.empty() || analysis.max_rgb_nits < 0.0) {
    set_diag(diagnostic, "HDR10+ generation requires valid frame analysis");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  Hdr10PlusWindow window;
  window.maxscl_r = window.maxscl_g = window.maxscl_b = analysis.max_rgb_nits;
  window.average_maxrgb = analysis.average_rgb_nits;
  for (std::size_t i = 0; i < analysis.percentile_nits.size(); ++i) {
    window.distribution_percentiles.push_back(
        100.0 * static_cast<double>(i + 1u) / static_cast<double>(analysis.percentile_nits.size()));
    window.distribution_values.push_back(analysis.percentile_nits[i]);
  }
  window.knee_x = 0.75;
  window.knee_y = 0.75;
  window.bezier_anchors = {0.75, 0.82, 0.9, 0.96, 1.0};
  metadata = {frame_index, timestamp_us, {std::move(window)}, scene_refresh};
  if (scene_refresh) ++telemetry_.scene_changes;
  ++telemetry_.dynamic_metadata_frames;
  set_diag(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult HdrEcosystem::package_metadata(const HdrFrameMetadata& metadata,
                                             HdrPacket& packet,
                                             std::string* diagnostic) {
  const auto validity = validate(metadata, diagnostic);
  if (validity != DIGITOR_RESULT_OK) {
    ++telemetry_.rejected_metadata;
    return validity;
  }
  std::string error;
  if (metadata.standard == HdrStandard::hdr10_plus) {
    if (callbacks_.package_hdr10_plus)
      return callbacks_.package_hdr10_plus(*metadata.hdr10_plus, packet, error);
    packet = {};
    packet.standard = HdrStandard::hdr10_plus;
    packet.transport = HdrMetadataTransport::dynamic_sei;
    packet.timestamp_us = metadata.hdr10_plus->timestamp_us;
    append_u32(packet.payload, static_cast<std::uint32_t>(metadata.hdr10_plus->windows.size()));
    set_diag(diagnostic, {});
    return DIGITOR_RESULT_OK;
  }
  if (metadata.standard == HdrStandard::dolby_vision) {
    if (!callbacks_.package_dolby_vision) {
      telemetry_.last_error = "Dolby Vision packaging requires a licensed adapter";
      set_diag(diagnostic, telemetry_.last_error);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    const auto result = callbacks_.package_dolby_vision(*metadata.dolby_vision, packet, error);
    set_diag(diagnostic, error);
    return result;
  }
  packet = {};
  packet.standard = metadata.standard;
  packet.transport = metadata.standard == HdrStandard::hdr10
                         ? HdrMetadataTransport::static_sei
                         : HdrMetadataTransport::container_side_data;
  set_diag(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

HdrEcosystemTelemetry HdrEcosystem::telemetry() const { return telemetry_; }

}  // namespace digitor
