#include "digitor/native_node_kernels.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace digitor {
namespace {
float smoothstep(float a,float b,float x){if(a==b)return x>=b?1.f:0.f;float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3.f-2.f*t);} 
}
void node_mixer_reference(std::span<const std::span<const Color>> inputs,
                          std::span<const float> weights,
                          std::span<Color> output){
 if(inputs.empty()||output.empty()) throw std::invalid_argument("node mixer requires inputs and output");
 for(auto in:inputs) if(in.size()!=output.size()) throw std::invalid_argument("node mixer dimensions mismatch");
 if(!weights.empty()&&weights.size()!=inputs.size()) throw std::invalid_argument("node mixer weight count mismatch");
 float total=0.f; if(weights.empty()) total=static_cast<float>(inputs.size()); else for(float w:weights) total+=std::max(0.f,w);
 if(total<=0.f) throw std::invalid_argument("node mixer weights must contain a positive value");
 for(std::size_t p=0;p<output.size();++p){Color c{};for(std::size_t i=0;i<inputs.size();++i){float w=weights.empty()?1.f:std::max(0.f,weights[i]);c.r+=inputs[i][p].r*w;c.g+=inputs[i][p].g*w;c.b+=inputs[i][p].b*w;c.a+=inputs[i][p].a*w;}output[p]={c.r/total,c.g/total,c.b/total,c.a/total};}
}
void power_window_matte_reference(const PowerWindowSettings&s,std::uint32_t w,std::uint32_t h,std::span<float> matte){
 if(!w||!h||matte.size()!=static_cast<std::size_t>(w)*h) throw std::invalid_argument("power window dimensions mismatch");
 const float cs=std::cos(-s.rotation),sn=std::sin(-s.rotation);const float fw=std::max(s.width,1e-6f),fh=std::max(s.height,1e-6f),feather=std::clamp(s.feather,0.f,1.f);
 for(std::uint32_t y=0;y<h;++y)for(std::uint32_t x=0;x<w;++x){float nx=(x+.5f)/w-s.center_x,ny=(y+.5f)/h-s.center_y;float rx=nx*cs-ny*sn,ry=nx*sn+ny*cs;float d=0.f;
  if(s.shape==WindowShape::ellipse){float q=std::sqrt((rx/(fw*.5f))*(rx/(fw*.5f))+(ry/(fh*.5f))*(ry/(fh*.5f)));d=1.f-q;}
  else if(s.shape==WindowShape::rectangle){float q=std::max(std::abs(rx)/(fw*.5f),std::abs(ry)/(fh*.5f));d=1.f-q;}
  else {d=.5f-ry/fh;}
  float m=feather<=0.f?(d>=0.f?1.f:0.f):smoothstep(-feather,feather,d);m*=std::clamp(s.opacity,0.f,1.f);if(s.invert)m=1.f-m;matte[static_cast<std::size_t>(y)*w+x]=std::clamp(m,0.f,1.f);
 }
}
void masked_composite_reference(std::span<const Color>a,std::span<const Color>b,std::span<const float>m,std::span<Color>o){
 if(a.size()!=b.size()||a.size()!=m.size()||a.size()!=o.size())throw std::invalid_argument("masked composite dimensions mismatch");
 for(std::size_t i=0;i<o.size();++i){float t=std::clamp(m[i],0.f,1.f);o[i]={a[i].r+(b[i].r-a[i].r)*t,a[i].g+(b[i].g-a[i].g)*t,a[i].b+(b[i].b-a[i].b)*t,a[i].a+(b[i].a-a[i].a)*t};}
}
}
