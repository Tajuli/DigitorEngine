#pragma once

#include "digitor/qualifier.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace digitor {

struct NativeHslQualifierParameters {
  std::array<float, 4> hue{};
  std::array<float, 4> saturation{};
  std::array<float, 4> luminance{};
  std::array<float, 4> cleanup{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t pixel_count{};
  std::uint32_t flags{};
};

inline constexpr std::uint32_t hsl_qualifier_flag_invert = 1u << 0;
inline constexpr std::uint32_t hsl_qualifier_flag_matte_output = 1u << 1;

[[nodiscard]] NativeHslQualifierParameters native_hsl_qualifier_parameters(
    const HslQualifierParameters&, std::uint32_t width,
    std::uint32_t height);

[[nodiscard]] std::string_view hsl_qualifier_shader_source() noexcept;
[[nodiscard]] std::string_view hsl_qualifier_shader_identity() noexcept;

} // namespace digitor
