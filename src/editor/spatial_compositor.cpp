#include "digitor/spatial_compositor.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace digitor { namespace {
float clamp01(float v){return std::clamp(v,0.0f,1.0f);} 
PixelF transparent(){return{};}
PixelF sample_nearest(const FrameF& f,double x,double y){
  const int ix=int(std::llround(x)), iy=int(std::llround(y));
  if(ix<0||iy<0||ix>=int(f.width)||iy>=int(f.height)) return transparent();
  return f.pixels[std::size_t(iy)*f.width+ix];
}
PixelF lerp(PixelF a,PixelF b,float t){return{a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t};}
PixelF sample_bilinear(const FrameF& f,double x,double y){
  const int x0=int(std::floor(x)), y0=int(std::floor(y)); const float tx=float(x-x0),ty=float(y-y0);
  auto at=[&](int px,int py){if(px<0||py<0||px>=int(f.width)||py>=int(f.height))return transparent();return f.pixels[std::size_t(py)*f.width+px];};
  return lerp(lerp(at(x0,y0),at(x0+1,y0),tx),lerp(at(x0,y0+1),at(x0+1,y0+1),tx),ty);
}
PixelF blend_pixel(PixelF d,PixelF s,BlendMode m,float opacity){
  s.a=clamp01(s.a*opacity); float br=s.r,bg=s.g,bb=s.b;
  if(m==BlendMode::add){br=clamp01(d.r+s.r);bg=clamp01(d.g+s.g);bb=clamp01(d.b+s.b);} 
  else if(m==BlendMode::multiply){br=d.r*s.r;bg=d.g*s.g;bb=d.b*s.b;}
  else if(m==BlendMode::screen){br=1-(1-d.r)*(1-s.r);bg=1-(1-d.g)*(1-s.g);bb=1-(1-d.b)*(1-s.b);} 
  const float oa=s.a+d.a*(1-s.a); if(oa<=0)return{};
  return{clamp01((br*s.a+d.r*d.a*(1-s.a))/oa),clamp01((bg*s.a+d.g*d.a*(1-s.a))/oa),clamp01((bb*s.a+d.b*d.a*(1-s.a))/oa),clamp01(oa)};
}
MotionSample motion_at(const std::vector<MotionSample>& m,std::int64_t frame){
  if(m.empty()) return{}; if(frame<=m.front().frame)return m.front(); if(frame>=m.back().frame)return m.back();
  auto hi=std::upper_bound(m.begin(),m.end(),frame,[](auto f,const MotionSample& s){return f<s.frame;}); auto lo=hi-1;
  const double t=double(frame-lo->frame)/double(hi->frame-lo->frame); MotionSample r; r.frame=frame;
  r.x=lo->x+(hi->x-lo->x)*t; r.y=lo->y+(hi->y-lo->y)*t; r.rotation_degrees=lo->rotation_degrees+(hi->rotation_degrees-lo->rotation_degrees)*t; r.scale=lo->scale+(hi->scale-lo->scale)*t; return r;
}
}

double evaluate(const AnimatedScalar& a,std::int64_t frame){
  if(a.keys.empty())return a.default_value; if(frame<=a.keys.front().frame)return a.keys.front().value; if(frame>=a.keys.back().frame)return a.keys.back().value;
  auto hi=std::upper_bound(a.keys.begin(),a.keys.end(),frame,[](auto f,const AnimationKey& k){return f<k.frame;});auto lo=hi-1;double t=double(frame-lo->frame)/double(hi->frame-lo->frame);return lo->value+(hi->value-lo->value)*t;
}
TransformState apply_stabilization(const TransformState& base,const StabilizationState& s,const std::vector<MotionSample>& track,std::int64_t frame){
  if(!s.enabled||track.empty())return base; auto m=motion_at(track,frame); TransformState out=base; const double strength=std::clamp(s.strength,0.0,1.0);
  out.position.x-=m.x*strength;out.position.y-=m.y*strength;out.rotation_degrees-=m.rotation_degrees*strength; const double zoom=std::max(1.0,1.0+std::clamp(s.crop_ratio,0.0,0.5));out.scale.x*=zoom/std::max(0.01,m.scale);out.scale.y*=zoom/std::max(0.01,m.scale);return out;
}
RenderResult render_spatial(const FrameF& input,std::uint32_t ow,std::uint32_t oh,const SpatialSettings& settings,const RenderPolicy& policy){
  if(!input.valid()||!ow||!oh)throw std::invalid_argument("invalid spatial frame");
  if(policy.backend!=VisualBackend::cpu&&!policy.gpu_available&&!policy.allow_cpu_fallback)return{{},false,"requested GPU backend unavailable and CPU fallback disabled"};
  FrameF out{ow,oh,std::vector<PixelF>(std::size_t(ow)*oh)};const auto&t=settings.transform;const auto&c=settings.crop.normalized;
  const double sx=std::max(std::abs(t.scale.x),1e-9),sy=std::max(std::abs(t.scale.y),1e-9);const double rad=t.rotation_degrees*3.14159265358979323846/180.0,cs=std::cos(rad),sn=std::sin(rad);
  for(std::uint32_t y=0;y<oh;++y)for(std::uint32_t x=0;x<ow;++x){
    double nx=(double(x)+.5)/ow-t.position.x-t.anchor.x,ny=(double(y)+.5)/oh-t.position.y-t.anchor.y;
    double rx=(cs*nx+sn*ny)/sx,ry=(-sn*nx+cs*ny)/sy;if(t.flip_x)rx=-rx;if(t.flip_y)ry=-ry;double u=rx+t.anchor.x,v=ry+t.anchor.y;
    if(u<c.left||u>c.right||v<c.top||v>c.bottom){out.pixels[std::size_t(y)*ow+x]={};continue;}
    double ix=u*input.width-.5,iy=v*input.height-.5;auto p=settings.sampling==SamplingMode::nearest?sample_nearest(input,ix,iy):sample_bilinear(input,ix,iy);p.a=clamp01(float(p.a*t.opacity));out.pixels[std::size_t(y)*ow+x]=p;
  }
  return{std::move(out),policy.backend!=VisualBackend::cpu&&policy.gpu_available,{}};
}
void apply_chroma_key(FrameF& f,const ChromaKeyState&s){
  if(!s.enabled)return;const float kr=float(s.key_r),kg=float(s.key_g),kb=float(s.key_b),sim=float(std::clamp(s.similarity,0.0,1.0)),soft=float(std::max(s.softness,1e-6)),spill=float(std::clamp(s.spill,0.0,1.0));
  for(auto&p:f.pixels){float d=std::sqrt((p.r-kr)*(p.r-kr)+(p.g-kg)*(p.g-kg)+(p.b-kb)*(p.b-kb))/1.7320508f;float matte=clamp01((d-sim)/soft);p.a*=matte;float dominance=std::max(0.0f,p.g-std::max(p.r,p.b));p.g=clamp01(p.g-dominance*spill*(1-matte));}
}
void composite(FrameF&d,const FrameF&f,BlendMode m,double opacity){if(!d.valid()||!f.valid()||d.width!=f.width||d.height!=f.height)throw std::invalid_argument("incompatible composite frames");for(std::size_t i=0;i<d.pixels.size();++i)d.pixels[i]=blend_pixel(d.pixels[i],f.pixels[i],m,float(std::clamp(opacity,0.0,1.0)));}
RenderResult render_layer(const FrameF&fg,const FrameF&bg,const LayerSettings&s,const StabilizationState&stab,const std::vector<MotionSample>&track,std::int64_t frame,const RenderPolicy&p){if(!bg.valid())throw std::invalid_argument("invalid background");auto spatial=s.spatial;spatial.transform=apply_stabilization(spatial.transform,stab,track,frame);auto r=render_spatial(fg,bg.width,bg.height,spatial,p);if(!r.frame.valid())return r;apply_chroma_key(r.frame,s.chroma_key);FrameF out=bg;composite(out,r.frame,s.blend,s.opacity);r.frame=std::move(out);return r;}
std::string stable_frame_digest(const FrameF&f){if(!f.valid())return{};std::uint64_t h=1469598103934665603ull;for(const auto&p:f.pixels){for(float v:{p.r,p.g,p.b,p.a}){std::uint32_t bits;std::memcpy(&bits,&v,sizeof(bits));for(int i=0;i<4;++i){h^=(bits>>(i*8))&255u;h*=1099511628211ull;}}}std::ostringstream o;o<<std::hex<<std::setw(16)<<std::setfill('0')<<h;return o.str();}
} // namespace digitor
