#include "gpu/native_primary_wheels.hpp"
namespace digitor {
NativePrimaryWheelsParameters native_primary_wheels_parameters(const PrimaryWheelsParameters&p,std::uint32_t count,std::uint32_t width,std::uint32_t height)noexcept{
 const auto&v=p.values();auto wheel=[](PrimaryRgb c,float m,bool e){return NativePrimaryWheel{{c.r,c.g,c.b},m,e?1u:0u,{}};};
 return {wheel(v.lift,v.lift_master,v.lift_enabled),wheel(v.gamma,v.gamma_master,v.gamma_enabled),wheel(v.gain,v.gain_master,v.gain_enabled),wheel(v.offset,v.offset_master,v.offset_enabled),count,width,height,0};
}
}
