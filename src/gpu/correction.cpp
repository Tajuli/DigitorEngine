#include "digitor/correction.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace digitor {
namespace {

void append_u32(std::string& out, std::uint32_t value) {
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned byte = 0; byte != 4; ++byte) {
    const auto v = (value >> (byte * 8)) & 0xffu;
    out.push_back(hex[v >> 4]);
    out.push_back(hex[v & 15]);
  }
}

void append_float(std::string& out, float value) {
  append_u32(out, std::bit_cast<std::uint32_t>(value));
}

void validate(const CorrectionSettings& settings) {
  if (settings.schema_version != correction_parameter_version)
    throw std::invalid_argument("unsupported Correction schema version");
  const float values[]{settings.exposure, settings.contrast, settings.saturation,
      settings.temperature, settings.tint, settings.highlights, settings.shadows,
      settings.hue, settings.color_boost};
  for (const float value : values)
    if (!std::isfinite(value) || value < -1.0f || value > 1.0f)
      throw std::invalid_argument("Correction values must be finite and in [-1, 1]");
}

std::string encode(const CorrectionSettings& settings) {
  std::string out = "correction:";
  append_u32(out, settings.schema_version);
  append_float(out, settings.exposure);
  append_float(out, settings.contrast);
  append_float(out, settings.saturation);
  append_float(out, settings.temperature);
  append_float(out, settings.tint);
  append_float(out, settings.highlights);
  append_float(out, settings.shadows);
  append_float(out, settings.hue);
  append_float(out, settings.color_boost);
  return out;
}

Color rotate_hue(Color color, float degrees) noexcept {
  if (degrees == 0.0f) return color;
  const float angle = degrees * 3.1415926535f / 180.0f;
  const float co = std::cos(angle), s = std::sin(angle);
  const float r = color.r, g = color.g, b = color.b;
  color.r = (.213f + co * .787f - s * .213f) * r +
      (.715f - co * .715f - s * .715f) * g +
      (.072f - co * .072f + s * .928f) * b;
  color.g = (.213f - co * .213f + s * .143f) * r +
      (.715f + co * .285f + s * .140f) * g +
      (.072f - co * .072f - s * .283f) * b;
  color.b = (.213f - co * .213f - s * .787f) * r +
      (.715f - co * .715f + s * .715f) * g +
      (.072f + co * .928f + s * .072f) * b;
  return color;
}

} // namespace

CorrectionParameters::CorrectionParameters(CorrectionSettings settings)
    : values_(settings), serialization_(encode(settings)), identity_(serialization_) {}

std::shared_ptr<const CorrectionParameters> CorrectionParameters::create(
    const CorrectionSettings& settings) {
  validate(settings);
  return std::shared_ptr<const CorrectionParameters>(new CorrectionParameters(settings));
}

bool CorrectionParameters::is_identity() const noexcept {
  const auto& p = values_;
  return p.exposure == 0.0f && p.contrast == 0.0f && p.saturation == 0.0f &&
      p.temperature == 0.0f && p.tint == 0.0f && p.highlights == 0.0f &&
      p.shadows == 0.0f && p.hue == 0.0f && p.color_boost == 0.0f;
}

Color apply_correction_reference(Color color,
                                 const CorrectionParameters& parameters) noexcept {
  if (parameters.is_identity()) return color;
  const auto& p = parameters.values();
  float r = color.r + p.temperature * .1f;
  float g = color.g + p.tint * .1f;
  float b = color.b - p.temperature * .1f;

  const float luminance = .2126f * r + .7152f * g + .0722f * b;
  const float maximum = std::max({r, g, b});
  const float minimum = std::min({r, g, b});
  const float chroma = maximum - minimum;
  const float saturation = std::max(0.0f, 1.0f + p.saturation);
  const float boost = 1.0f + p.color_boost * (1.0f - std::clamp(chroma, 0.0f, 1.0f));
  const float combined_saturation = std::max(0.0f, saturation * boost);
  r = luminance + (r - luminance) * combined_saturation;
  g = luminance + (g - luminance) * combined_saturation;
  b = luminance + (b - luminance) * combined_saturation;

  const float contrast = std::max(0.0f, 1.0f + p.contrast);
  r = (r - .5f) * contrast + .5f;
  g = (g - .5f) * contrast + .5f;
  b = (b - .5f) * contrast + .5f;

  const auto tone = [&](float value) noexcept {
    const float shadow_weight = 1.0f - std::clamp(value, 0.0f, 1.0f);
    const float highlight_weight = std::clamp(value, 0.0f, 1.0f);
    return value + p.shadows * shadow_weight * .25f +
        p.highlights * highlight_weight * .25f;
  };
  r = tone(r);
  g = tone(g);
  b = tone(b);

  const float exposure = std::exp2(p.exposure * 2.0f);
  Color result{r * exposure, g * exposure, b * exposure, color.a};
  return rotate_hue(result, p.hue * 180.0f);
}

void apply_correction_reference(std::span<const Color> input,
                                std::span<Color> output,
                                const CorrectionParameters& parameters) {
  if (input.size() != output.size())
    throw std::invalid_argument("Correction image sizes differ");
  for (std::size_t index = 0; index < input.size(); ++index)
    output[index] = apply_correction_reference(input[index], parameters);
}

} // namespace digitor
