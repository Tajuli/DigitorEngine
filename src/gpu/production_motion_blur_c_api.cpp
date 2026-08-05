#include "digitor/production_motion_blur.hpp"
#include <cstddef>
#include <cstdint>
extern "C" {
struct DigitorMotionBlurSettings { float shutter_angle; std::uint32_t samples; float motion_scale; float confidence_floor; std::uint32_t center_exposure; };
std::uint32_t digitor_motion_blur_rgba32f(const float* in,const float* motion,float* out,std::uint32_t w,std::uint32_t h,const DigitorMotionBlurSettings* c,std::uint64_t* digest){ if(!in||!motion||!out||!c||!digest||w==0||h==0) return 1u; const std::size_t n=std::size_t(w)*h; digitor::MotionBlurFrame input{w,h,{}}; input.pixels.resize(n); std::vector<digitor::MotionVector> mv(n); for(std::size_t i=0;i<n;++i){ input.pixels[i]={in[i*4],in[i*4+1],in[i*4+2],in[i*4+3]}; mv[i]={motion[i*3],motion[i*3+1],motion[i*3+2]}; } digitor::MotionBlurSettings s; s.shutter_angle=c->shutter_angle;s.samples=c->samples;s.motion_scale=c->motion_scale;s.confidence_floor=c->confidence_floor;s.center_exposure=c->center_exposure!=0u; digitor::MotionBlurFrame output; auto r=digitor::apply_motion_blur_reference(input,mv,output,s); if(r.status!=digitor::MotionBlurStatus::ready) return 2u; for(std::size_t i=0;i<n;++i){ out[i*4]=output.pixels[i].r;out[i*4+1]=output.pixels[i].g;out[i*4+2]=output.pixels[i].b;out[i*4+3]=output.pixels[i].a; } *digest=r.digest; return 0u; }
}
