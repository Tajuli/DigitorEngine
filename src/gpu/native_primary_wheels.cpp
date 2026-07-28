#include "gpu/native_primary_wheels.hpp"
namespace digitor {
NativePrimaryWheelsParameters native_primary_wheels_parameters(const PrimaryWheelsParameters&p,std::uint32_t count,std::uint32_t width,std::uint32_t height)noexcept{
 const auto&v=p.values();return {{v.lift.r,v.lift.g,v.lift.b,v.lift_master},
  {v.gamma.r,v.gamma.g,v.gamma.b,v.gamma_master},{v.gain.r,v.gain.g,v.gain.b,v.gain_master},
  {v.offset.r,v.offset.g,v.offset.b,v.offset_master},
  {v.lift_enabled?1u:0u,v.gamma_enabled?1u:0u,v.gain_enabled?1u:0u,v.offset_enabled?1u:0u},
  count,width,height,0};
}
}
