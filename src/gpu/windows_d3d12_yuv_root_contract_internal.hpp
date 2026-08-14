#pragma once

#include <cstdint>

namespace digitor::internal {

struct WindowsD3D12YuvConstantsLayout {
  float y_offset, y_scale, uv_offset, uv_scale;
  float row_r[3], p0, row_g[3], p1, row_b[3], output_scale;
  std::uint32_t width, height, bit_depth, full_range;
};

struct WindowsD3D12YuvRootConstantsContract {
  std::uint32_t shader_register;
  std::uint32_t register_space;
  std::uint32_t num_32bit_values;
};

inline constexpr WindowsD3D12YuvRootConstantsContract
    kWindowsD3D12YuvRootConstants{0, 0,
                                  sizeof(WindowsD3D12YuvConstantsLayout) /
                                      sizeof(std::uint32_t)};

static_assert(sizeof(WindowsD3D12YuvConstantsLayout) == 80);
static_assert(kWindowsD3D12YuvRootConstants.shader_register == 0);
static_assert(kWindowsD3D12YuvRootConstants.register_space == 0);
static_assert(kWindowsD3D12YuvRootConstants.num_32bit_values == 20);

} // namespace digitor::internal
