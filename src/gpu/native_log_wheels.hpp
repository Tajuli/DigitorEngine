#pragma once
#include "digitor/log_wheels.hpp"
namespace digitor {
struct alignas(16) NativeLogWheelsParameters {
  float shadows[4],midtones[4],highlights[4],global[4];
  std::uint32_t enabled[4];
  float tonal[4];
  std::uint32_t pixel_count,width,height,padding{};
};
static_assert(sizeof(NativeLogWheelsParameters)==112);
NativeLogWheelsParameters native_log_wheels_parameters(const LogWheelsParameters&,
    std::uint32_t,std::uint32_t,std::uint32_t) noexcept;
}
