#include "digitor/qualifier.hpp"
#include "core/numeric_utils.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace digitor {
namespace {
std::atomic_uint64_t reference_calls{};

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

void validate_range(const QualifierRange& value, const char* name) {
  if (!std::isfinite(value.low) || !std::isfinite(value.high) ||
      !std::isfinite(value.softness) || value.low < 0.0f || value.low > 1.0f ||
      value.high < 0.0f || value.high > 1.0f || value.softness < 0.0f ||
      value.softness > 1.0f)
    throw std::invalid_argument(std::string("invalid qualifier ") + name);
}
void validate(const QualifierSettings& settings) {
  if (settings.schema_version != hsl_qualifier_parameter_version)
    throw std::invalid_argument("unsupported HSL qualifier schema version");
  validate_range(settings.hue, "hue");
  validate_range(settings.saturation, "saturation");
  validate_range(settings.luminance, "luminance");
  if (!std::isfinite(settings.blur) || !std::isfinite(settings.denoise) ||
      !std::isfinite(settings.clean_black) || !std::isfinite(settings.clean_white) ||
      settings.blur < 0.0f || settings.blur > 64.0f || settings.denoise < 0.0f ||
      settings.denoise > 1.0f || settings.clean_black < 0.0f ||
      settings.clean_black > 1.0f || settings.clean_white < 0.0f ||
      settings.clean_white > 1.0f)
    throw std::invalid_argument("invalid qualifier cleanup setting");
}
std::string encode(const QualifierSettings& p) {
  std::string out = "hsl-qualifier:";
  append_u32(out, p.schema_version);
  const auto range = [&](const QualifierRange& r) {
    append_float(out, r.low); append_float(out, r.high); append_float(out, r.softness);
  };
  range(p.hue); range(p.saturation); range(p.luminance);
  append_float(out, p.blur); append_float(out, p.denoise);
  append_float(out, p.clean_black); append_float(out, p.clean_white);
  append_u32(out, p.invert ? 1u : 0u);
  append_u32(out, p.matte_output ? 1u : 0u);
  return out;
}
float linear_weight(float value, const QualifierRange& range) noexcept {
  if (value >= range.low && value <= range.high) return 1.0f;
  if (range.softness > 0.0f && value < range.low && value > range.low - range.softness)
    return (value - range.low + range.softness) / range.softness;
  if (range.softness > 0.0f && value > range.high && value < range.high + range.softness)
    return (range.high + range.softness - value) / range.softness;
  return 0.0f;
}
float hue_weight(float hue, const QualifierRange& range) noexcept {
  if (range.low <= range.high) return linear_weight(hue, range);
  return std::max(linear_weight(hue, {range.low, 1.0f, range.softness}),
                  linear_weight(hue, {0.0f, range.high, range.softness}));
}
void rgb_to_hsl(Color color, float& hue, float& saturation, float& luminance) noexcept {
  const float high = std::max({color.r, color.g, color.b});
  const float low = std::min({color.r, color.g, color.b});
  const float delta = high - low;
  luminance = (high + low) * 0.5f;
  saturation = delta == 0.0f ? 0.0f :
      delta / std::max(1.0e-8f, 1.0f - std::abs(2.0f * luminance - 1.0f));
  hue = 0.0f;
  if (delta == 0.0f) return;
  if (high == color.r) hue = std::fmod((color.g - color.b) / delta, 6.0f);
  else if (high == color.g) hue = (color.b - color.r) / delta + 2.0f;
  else hue = (color.r - color.g) / delta + 4.0f;
  hue /= 6.0f;
  if (hue < 0.0f) hue += 1.0f;
}
float base_matte(Color color, const QualifierSettings& settings) noexcept {
  if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b))
    return 0.0f;
  float h{}, s{}, l{};
  rgb_to_hsl(color, h, s, l);
  float value = hue_weight(h, settings.hue) *
      linear_weight(s, settings.saturation) * linear_weight(l, settings.luminance);
  if (value <= settings.clean_black) value = 0.0f;
  if (value >= 1.0f - settings.clean_white) value = 1.0f;
  return settings.invert ? 1.0f - value : value;
}
} // namespace

HslQualifierParameters::HslQualifierParameters(QualifierSettings settings)
    : values_(settings), serialization_(encode(settings)), identity_(serialization_) {}
std::shared_ptr<const HslQualifierParameters> HslQualifierParameters::create(
    const QualifierSettings& settings) {
  validate(settings);
  return std::shared_ptr<const HslQualifierParameters>(new HslQualifierParameters(settings));
}
bool HslQualifierParameters::is_identity() const noexcept {
  const auto& p = values_;
  return p.hue.low == 0.0f && p.hue.high == 1.0f && p.hue.softness == 0.0f &&
      p.saturation.low == 0.0f && p.saturation.high == 1.0f &&
      p.saturation.softness == 0.0f && p.luminance.low == 0.0f &&
      p.luminance.high == 1.0f && p.luminance.softness == 0.0f &&
      p.blur == 0.0f && p.denoise == 0.0f && p.clean_black == 0.0f &&
      p.clean_white == 0.0f && !p.invert;
}
void HslQualifier::set_settings(QualifierSettings settings) {
  validate(settings); settings_ = settings;
}
void HslQualifier::sample(Color color) {
  float h{}, s{}, l{}; rgb_to_hsl(color, h, s, l);
  settings_.hue = {h, h, 0.05f};
  settings_.saturation = {s, s, 0.1f};
  settings_.luminance = {l, l, 0.1f};
}
void HslQualifier::sample(std::span<const Color> colors) {
  if (colors.empty()) throw std::invalid_argument("eye dropper sample is empty");
  double sine{}, cosine{}, saturation{}, luminance{};
  constexpr double tau = 6.28318530717958647692;
  for (const auto color : colors) {
    float h{}, s{}, l{}; rgb_to_hsl(color, h, s, l);
    sine += std::sin(h * tau); cosine += std::cos(h * tau);
    saturation += s; luminance += l;
  }
  float hue = static_cast<float>(std::atan2(sine, cosine) / tau);
  if (hue < 0.0f) hue += 1.0f;
  const float count = checked_size_to_float(colors.size());
  settings_.hue = {hue, hue, 0.05f};
  settings_.saturation = {static_cast<float>(saturation) / count,
      static_cast<float>(saturation) / count, 0.1f};
  settings_.luminance = {static_cast<float>(luminance) / count,
      static_cast<float>(luminance) / count, 0.1f};
}
float apply_hsl_qualifier_reference(Color color,
    const HslQualifierParameters& parameters) noexcept {
  reference_calls.fetch_add(1, std::memory_order_relaxed);
  return base_matte(color, parameters.values());
}
void apply_hsl_qualifier_reference(std::span<const Color> input,
    std::span<float> output, const HslQualifierParameters& parameters) {
  if (input.size() != output.size())
    throw std::invalid_argument("HSL qualifier image sizes differ");
  for (std::size_t i = 0; i < input.size(); ++i)
    output[i] = apply_hsl_qualifier_reference(input[i], parameters);
}
std::uint64_t hsl_qualifier_reference_count() noexcept {
  return reference_calls.load(std::memory_order_relaxed);
}
void reset_hsl_qualifier_reference_count() noexcept {
  reference_calls.store(0, std::memory_order_relaxed);
}
std::vector<float> HslQualifier::matte_cpu(std::span<const Color> input,
    std::uint32_t width, std::uint32_t height) const {
  if (input.size() != static_cast<std::size_t>(width) * height)
    throw std::invalid_argument("image size does not match dimensions");
  const auto parameters = HslQualifierParameters::create(settings_);
  std::vector<float> matte(input.size());
  apply_hsl_qualifier_reference(input, matte, *parameters);
  if (settings_.denoise > 0.0f && width && height) {
    const auto source = matte;
    for (std::uint32_t y = 0; y < height; ++y) for (std::uint32_t x = 0; x < width; ++x) {
      std::array<float, 9> window{}; std::size_t count{};
      for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        const auto yy = std::clamp<int>(static_cast<int>(y) + dy, 0, static_cast<int>(height) - 1);
        const auto xx = std::clamp<int>(static_cast<int>(x) + dx, 0, static_cast<int>(width) - 1);
        window[count++] = source[static_cast<std::size_t>(yy) * width + static_cast<std::size_t>(xx)];
      }
      std::nth_element(window.begin(), window.begin() + 4, window.end());
      const auto index = static_cast<std::size_t>(y) * width + x;
      matte[index] += settings_.denoise * (window[4] - matte[index]);
    }
  }
  const auto radius = static_cast<std::uint32_t>(std::ceil(settings_.blur));
  if (radius && width && height) {
    const auto source = matte;
    for (std::uint32_t y = 0; y < height; ++y) for (std::uint32_t x = 0; x < width; ++x) {
      double sum{}; std::size_t count{};
      for (int dy = -static_cast<int>(radius); dy <= static_cast<int>(radius); ++dy)
        for (int dx = -static_cast<int>(radius); dx <= static_cast<int>(radius); ++dx) {
          const auto yy = std::clamp<int>(static_cast<int>(y) + dy, 0, static_cast<int>(height) - 1);
          const auto xx = std::clamp<int>(static_cast<int>(x) + dx, 0, static_cast<int>(width) - 1);
          sum += source[static_cast<std::size_t>(yy) * width + static_cast<std::size_t>(xx)]; ++count;
        }
      matte[static_cast<std::size_t>(y) * width + x] =
          static_cast<float>(sum / checked_size_to_double(count));
    }
  }
  return matte;
}
void HslQualifier::matte_gpu(CommandEncoder&, std::span<const Color>,
    std::span<float>, std::uint32_t, std::uint32_t) const {
  throw std::logic_error(
      "HSL qualifier GPU execution requires a native backend; CPU fallback is forbidden");
}

} // namespace digitor
