#pragma once
#include "digitor/primary_wheels.hpp"
namespace digitor {
struct alignas(16) NativePrimaryWheel { float rgb[3]; float master; std::uint32_t enabled; std::uint32_t padding[3]{}; };
struct alignas(16) NativePrimaryWheelsParameters { NativePrimaryWheel lift,gamma,gain,offset; std::uint32_t pixel_count,width,height,padding{}; };
NativePrimaryWheelsParameters native_primary_wheels_parameters(const PrimaryWheelsParameters&,std::uint32_t,std::uint32_t,std::uint32_t) noexcept;
}
