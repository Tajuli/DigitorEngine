#include "gpu/native_log_wheels.hpp"
namespace digitor {
NativeLogWheelsParameters native_log_wheels_parameters(const LogWheelsParameters&p,std::uint32_t count,std::uint32_t width,std::uint32_t height)noexcept{
 const auto&v=p.values();return {{v.shadows.rgb.r,v.shadows.rgb.g,v.shadows.rgb.b,v.shadows.master},
 {v.midtones.rgb.r,v.midtones.rgb.g,v.midtones.rgb.b,v.midtones.master},
 {v.highlights.rgb.r,v.highlights.rgb.g,v.highlights.rgb.b,v.highlights.master},
 {v.global.rgb.r,v.global.rgb.g,v.global.rgb.b,v.global.master},
 {v.shadows.enabled?1u:0u,v.midtones.enabled?1u:0u,v.highlights.enabled?1u:0u,v.global.enabled?1u:0u},
 {v.shadow_pivot,v.highlight_pivot,v.transition_width,0},count,width,height,0};
}
}
