#include "digitor/broadcast_color_matching.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace digitor {
namespace {

void set_diagnostic(std::string* diagnostic, std::string value) {
  if (diagnostic) *diagnostic = std::move(value);
}

bool finite(Color value) {
  return std::isfinite(value.r) && std::isfinite(value.g) &&
         std::isfinite(value.b) && std::isfinite(value.a);
}

double luma(Color value, BroadcastStandard standard) {
  if (standard == BroadcastStandard::rec601)
    return 0.299 * value.r + 0.587 * value.g + 0.114 * value.b;
  if (standard == BroadcastStandard::rec2020_sdr ||
      standard == BroadcastStandard::rec2020_pq ||
      standard == BroadcastStandard::rec2020_hlg)
    return 0.2627 * value.r + 0.6780 * value.g + 0.0593 * value.b;
  return 0.2126 * value.r + 0.7152 * value.g + 0.0722 * value.b;
}

std::array<double, 3> mean(std::span<const Color> pixels) {
  std::array<double, 3> result{};
  for (const auto& pixel : pixels) {
    result[0] += pixel.r;
    result[1] += pixel.g;
    result[2] += pixel.b;
  }
  const double count = static_cast<double>(pixels.size());
  for (auto& value : result) value /= count;
  return result;
}

double distance(Color a, Color b) {
  const double dr = static_cast<double>(a.r) - b.r;
  const double dg = static_cast<double>(a.g) - b.g;
  const double db = static_cast<double>(a.b) - b.b;
  return std::sqrt(dr * dr + dg * dg + db * db);
}

Color apply_transform(Color input, const ColorMatchTransform& transform,
                      double strength) {
  const double r = transform.matrix[0] * input.r +
                   transform.matrix[1] * input.g +
                   transform.matrix[2] * input.b;
  const double g = transform.matrix[3] * input.r +
                   transform.matrix[4] * input.g +
                   transform.matrix[5] * input.b;
  const double b = transform.matrix[6] * input.r +
                   transform.matrix[7] * input.g +
                   transform.matrix[8] * input.b;
  Color matched{
      static_cast<float>((r + transform.offset[0]) * transform.gain[0]),
      static_cast<float>((g + transform.offset[1]) * transform.gain[1]),
      static_cast<float>((b + transform.offset[2]) * transform.gain[2]), input.a};
  const float mix = static_cast<float>(std::clamp(strength, 0.0, 1.0));
  matched.r = input.r + (matched.r - input.r) * mix;
  matched.g = input.g + (matched.g - input.g) * mix;
  matched.b = input.b + (matched.b - input.b) * mix;
  return matched;
}

}  // namespace

DigitorResult BroadcastColorSuite::monitor(
    std::span<const Color> pixels, std::uint32_t width, std::uint32_t height,
    const BroadcastMonitorConfig& config, BroadcastMonitorResult& result,
    std::string* diagnostic) {
  if (pixels.empty() || width == 0u || height == 0u ||
      static_cast<std::size_t>(width) * height != pixels.size() ||
      !std::isfinite(config.reference_white_nits) ||
      !std::isfinite(config.peak_nits) || config.reference_white_nits <= 0.0 ||
      config.peak_nits <= 0.0 || config.safe_area.left < 0.0 ||
      config.safe_area.right < 0.0 || config.safe_area.top < 0.0 ||
      config.safe_area.bottom < 0.0 ||
      config.safe_area.left + config.safe_area.right >= 1.0 ||
      config.safe_area.top + config.safe_area.bottom >= 1.0) {
    telemetry_.last_error = "invalid broadcast monitor input or configuration";
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  result = {};
  result.width = width;
  result.height = height;
  result.analyzed_pixels = pixels.size();
  result.minimum_luma = std::numeric_limits<double>::infinity();
  result.maximum_luma = -std::numeric_limits<double>::infinity();
  if (config.generate_warning_mask) result.warning_mask.resize(pixels.size(), 0u);

  const double black = config.range == BroadcastRange::legal ? 16.0 / 255.0 : 0.0;
  const double white = config.range == BroadcastRange::legal ? 235.0 / 255.0 : 1.0;
  const double chroma_limit = config.range == BroadcastRange::legal ? 112.0 / 255.0 : 0.5;
  double luma_sum{};

  for (std::size_t index = 0; index < pixels.size(); ++index) {
    const Color pixel = pixels[index];
    bool warning{};
    if (!finite(pixel)) {
      ++result.violations.non_finite;
      warning = true;
      if (config.generate_warning_mask) result.warning_mask[index] = 0xffu;
      continue;
    }
    const double y = luma(pixel, config.standard);
    const double cb = (pixel.b - y) * 0.5;
    const double cr = (pixel.r - y) * 0.5;
    result.minimum_luma = std::min(result.minimum_luma, y);
    result.maximum_luma = std::max(result.maximum_luma, y);
    luma_sum += y;

    if (config.detect_luma_excursions && y < black) {
      ++result.violations.below_black;
      warning = true;
    }
    if (config.detect_luma_excursions && y > white) {
      ++result.violations.above_white;
      warning = true;
    }
    if (config.detect_chroma_excursions &&
        (std::abs(cb) > chroma_limit || std::abs(cr) > chroma_limit)) {
      ++result.violations.chroma_illegal;
      warning = true;
    }
    if (config.detect_gamut_excursions &&
        (pixel.r < 0.0f || pixel.r > 1.0f || pixel.g < 0.0f ||
         pixel.g > 1.0f || pixel.b < 0.0f || pixel.b > 1.0f)) {
      ++result.violations.gamut_illegal;
      warning = true;
    }
    if (warning && config.generate_warning_mask) result.warning_mask[index] = 0xffu;
  }

  if (!std::isfinite(result.minimum_luma)) result.minimum_luma = 0.0;
  if (!std::isfinite(result.maximum_luma)) result.maximum_luma = 0.0;
  result.average_luma = luma_sum / static_cast<double>(pixels.size());
  result.peak_nits = std::max(0.0, result.maximum_luma) * config.peak_nits;
  const auto& v = result.violations;
  result.broadcast_safe = v.below_black == 0u && v.above_white == 0u &&
                          v.chroma_illegal == 0u && v.gamut_illegal == 0u &&
                          v.non_finite == 0u;
  ++telemetry_.monitored_frames;
  telemetry_.monitored_pixels += pixels.size();
  if (!result.broadcast_safe) ++telemetry_.unsafe_frames;
  telemetry_.last_error.clear();
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult BroadcastColorSuite::build_match(
    const ColorMatchRequest& request, ColorMatchTransform& transform,
    std::string* diagnostic) {
  ++telemetry_.match_requests;
  if (!std::isfinite(request.strength) || request.strength < 0.0 ||
      request.strength > 1.0) {
    ++telemetry_.rejected_requests;
    telemetry_.last_error = "color-match strength must be within zero and one";
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  std::vector<Color> measured;
  std::vector<Color> reference;
  if (request.method == MatchMethod::chart_24_patch) {
    if (request.chart_patches.size() < 6u) {
      ++telemetry_.rejected_requests;
      telemetry_.last_error = "chart matching requires at least six valid patches";
      set_diagnostic(diagnostic, telemetry_.last_error);
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    measured.reserve(request.chart_patches.size());
    reference.reserve(request.chart_patches.size());
    for (const auto& patch : request.chart_patches) {
      if (!finite(patch.measured) || !finite(patch.reference) ||
          !std::isfinite(patch.weight) || patch.weight <= 0.0) {
        ++telemetry_.rejected_requests;
        telemetry_.last_error = "chart patches must contain finite colors and positive weights";
        set_diagnostic(diagnostic, telemetry_.last_error);
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      measured.push_back(patch.measured);
      reference.push_back(patch.reference);
    }
  } else {
    if (request.source.empty() || request.reference.empty()) {
      ++telemetry_.rejected_requests;
      telemetry_.last_error = "reference matching requires non-empty source and reference samples";
      set_diagnostic(diagnostic, telemetry_.last_error);
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    measured.assign(request.source.begin(), request.source.end());
    reference.assign(request.reference.begin(), request.reference.end());
  }

  for (const auto& pixel : measured) {
    if (!finite(pixel)) {
      ++telemetry_.rejected_requests;
      telemetry_.last_error = "source matching samples contain non-finite values";
      set_diagnostic(diagnostic, telemetry_.last_error);
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
  }
  for (const auto& pixel : reference) {
    if (!finite(pixel)) {
      ++telemetry_.rejected_requests;
      telemetry_.last_error = "reference matching samples contain non-finite values";
      set_diagnostic(diagnostic, telemetry_.last_error);
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
  }

  const auto source_mean = mean(measured);
  const auto reference_mean = mean(reference);
  transform = {};
  for (std::size_t channel = 0; channel < 3u; ++channel) {
    const double denominator = std::abs(source_mean[channel]) < 1e-9
                                   ? 1.0
                                   : source_mean[channel];
    transform.gain[channel] = reference_mean[channel] / denominator;
    transform.gain[channel] = std::clamp(transform.gain[channel], 0.125, 8.0);
  }
  if (request.preserve_luminance) {
    const double source_y = 0.2126 * source_mean[0] + 0.7152 * source_mean[1] +
                            0.0722 * source_mean[2];
    const double matched_y = 0.2126 * source_mean[0] * transform.gain[0] +
                             0.7152 * source_mean[1] * transform.gain[1] +
                             0.0722 * source_mean[2] * transform.gain[2];
    if (matched_y > 1e-9) {
      const double correction = source_y / matched_y;
      for (auto& gain : transform.gain) gain *= correction;
    }
  }

  const std::size_t pair_count = std::min(measured.size(), reference.size());
  double before{};
  double after{};
  for (std::size_t index = 0; index < pair_count; ++index) {
    before += distance(measured[index], reference[index]);
    after += distance(apply_transform(measured[index], transform, request.strength),
                      reference[index]);
  }
  transform.mean_error_before = before / static_cast<double>(pair_count);
  transform.mean_error_after = after / static_cast<double>(pair_count);
  transform.confidence = transform.mean_error_before <= 1e-12
                             ? 1.0
                             : std::clamp(1.0 - transform.mean_error_after /
                                                   transform.mean_error_before,
                                          0.0, 1.0);
  telemetry_.last_error.clear();
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult BroadcastColorSuite::apply_match(
    std::span<const Color> source, std::span<Color> destination,
    const ColorMatchTransform& transform, double strength,
    std::string* diagnostic) {
  if (source.empty() || source.size() != destination.size() ||
      !std::isfinite(strength) || strength < 0.0 || strength > 1.0) {
    telemetry_.last_error = "matching requires equal non-empty spans and valid strength";
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  for (std::size_t index = 0; index < source.size(); ++index) {
    if (!finite(source[index])) {
      telemetry_.last_error = "matching input contains non-finite pixels";
      set_diagnostic(diagnostic, telemetry_.last_error);
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    destination[index] = apply_transform(source[index], transform, strength);
  }
  telemetry_.matched_pixels += source.size();
  telemetry_.last_error.clear();
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

}  // namespace digitor
