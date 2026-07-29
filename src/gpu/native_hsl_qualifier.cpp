#include "gpu/native_hsl_qualifier.hpp"
#include "hsl_qualifier_shader.hpp"
#include <limits>
#include <stdexcept>
namespace digitor {
NativeHslQualifierParameters native_hsl_qualifier_parameters(
    const HslQualifierParameters& parameters, std::uint32_t width,
    std::uint32_t height) {
  if (!width || !height) throw std::invalid_argument("HSL qualifier dimensions must be non-zero");
  const auto count = static_cast<std::uint64_t>(width) * height;
  if (count > std::numeric_limits<std::uint32_t>::max())
    throw std::overflow_error("HSL qualifier pixel count exceeds native contract");
  const auto& p = parameters.values();
  NativeHslQualifierParameters n;
  n.hue = {p.hue.low,p.hue.high,p.hue.softness,0};
  n.saturation = {p.saturation.low,p.saturation.high,p.saturation.softness,0};
  n.luminance = {p.luminance.low,p.luminance.high,p.luminance.softness,0};
  n.cleanup = {p.clean_black,p.clean_white,p.denoise,p.blur};
  n.width=width;n.height=height;n.pixel_count=static_cast<std::uint32_t>(count);
  n.flags=(p.invert?hsl_qualifier_flag_invert:0u)|
      (p.matte_output?hsl_qualifier_flag_matte_output:0u);
  return n;
}
std::string_view hsl_qualifier_shader_source() noexcept { return digitor_hsl_qualifier_hlsl; }
std::string_view hsl_qualifier_shader_identity() noexcept {
  return "digitor-hsl-qualifier-v5.0.0-schema1";
}
} // namespace digitor
