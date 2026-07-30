#pragma once

#include "digitor/native_node_shader_contracts.hpp"

#include <array>
#include <cstdint>

namespace digitor {

inline constexpr std::array<NativeNodeBinding, 3> kNativeHslMatteBindings{{
    {0, NativeNodeBindingKind::sampled_or_storage_input, "rgba32f"},
    {1, NativeNodeBindingKind::storage_output, "r32f"},
    {2, NativeNodeBindingKind::constants, "80-bytes"},
}};

inline constexpr std::array<NativeNodeBinding, 2> kNativePowerWindowMatteBindings{{
    {0, NativeNodeBindingKind::storage_output, "r32f"},
    {1, NativeNodeBindingKind::constants, "48-bytes"},
}};

inline constexpr std::array<NativeNodeBinding, 4> kNativeMatteMultiplyBindings{{
    {0, NativeNodeBindingKind::sampled_or_storage_input, "r32f"},
    {1, NativeNodeBindingKind::sampled_or_storage_input, "r32f"},
    {2, NativeNodeBindingKind::storage_output, "r32f"},
    {3, NativeNodeBindingKind::constants, "8-bytes"},
}};

inline constexpr std::uint32_t kNativeHslMatteConstantBytes = 80;
inline constexpr std::uint32_t kNativePowerWindowMatteConstantBytes = 48;
inline constexpr std::uint32_t kNativeMatteMultiplyConstantBytes = 8;

[[nodiscard]] constexpr bool is_native_node_mask_kernel(
    NativeNodeKernel kernel) noexcept {
  return kernel == NativeNodeKernel::hsl_matte ||
         kernel == NativeNodeKernel::power_window_matte ||
         kernel == NativeNodeKernel::matte_multiply ||
         kernel == NativeNodeKernel::masked_composite;
}

} // namespace digitor
