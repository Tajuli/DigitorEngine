#pragma once
#include "digitor/primary_wheels.hpp"
namespace digitor {
// Keep the shader ABI in complete 16-byte registers. Nested float3 structs have
// different layout rules in HLSL and Metal and must never cross this boundary.
struct alignas(16) NativePrimaryWheelsParameters {
  float lift[4],gamma[4],gain[4],offset[4];
  std::uint32_t enabled[4];
  std::uint32_t pixel_count,width,height,padding{};
};
static_assert(sizeof(NativePrimaryWheelsParameters)==96);
NativePrimaryWheelsParameters native_primary_wheels_parameters(const PrimaryWheelsParameters&,std::uint32_t,std::uint32_t,std::uint32_t) noexcept;
}
