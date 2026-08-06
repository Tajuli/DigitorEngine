#include "digitor/production_video_denoise.hpp"
#include "digitor/cpu_parallel_executor.hpp"

#include <algorithm>
#include <cmath>

namespace digitor {
namespace {
float c01(float v) noexcept { return std::clamp(v,0.0f,1.0f); }
float lum(const DenoisePixel& p) noexcept { return .2126f*p.r+.7152f*p.g+.0722f*p.b; }
bool ok(const DenoiseSettings& s) noexcept { return s.spatial_strength>=0&&s.spatial_strength<=1&&s.temporal_strength>=0&&s.temporal_strength<=1&&s.luma_threshold>0&&s.chroma_threshold>0&&s.detail_preservation>=0&&s.detail_preservation<=1&&s.motion_sensitivity>=0&&s.motion_sensitivity<=1; }
std::uint64_t hash(std::uint64_t h,const void* p,std::size_t n) noexcept { const auto* b=static_cast<const unsigned char*>(p); for(std::size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ull;} return h; }
}
std::uint64_t denoise_frame_digest(const DenoiseFrame& f) noexcept { std::uint64_t h=1469598103934665603ull; h=hash(h,&f.width,sizeof(f.width)); h=hash(h,&f.height,sizeof(f.height)); if(!f.pixels.empty()) h=hash(h,f.pixels.data(),f.pixels.size()*sizeof(DenoisePixel)); return h; }
DenoiseResult apply_video_denoise_reference(const DenoiseFrame& cur,const DenoiseFrame* prev,const std::vector<float>* motion,DenoiseFrame& out,const DenoiseSettings& s){
 DenoiseResult r; const std::size_t n=std::size_t(cur.width)*cur.height; if(!ok(s)||cur.width==0||cur.height==0||cur.pixels.size()!=n)return r; if(prev&&(prev->width!=cur.width||prev->height!=cur.height||prev->pixels.size()!=n))return r; if(motion&&motion->size()!=n)return r;
 out={cur.width,cur.height,{}}; out.pixels.resize(n);
 const double d=shared_cpu_executor().deterministic_reduce<double>(n,16384,0.0,[&](std::size_t begin,std::size_t end){double partial=0.0;for(std::size_t i=begin;i<end;++i){const std::uint32_t y=static_cast<std::uint32_t>(i/cur.width),x=static_cast<std::uint32_t>(i%cur.width);const auto c=cur.pixels[i];DenoisePixel sum{};float wsum=0;
  for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox){const auto sx=static_cast<std::uint32_t>(std::clamp<int>(static_cast<int>(x)+ox,0,static_cast<int>(cur.width)-1)); const auto sy=static_cast<std::uint32_t>(std::clamp<int>(static_cast<int>(y)+oy,0,static_cast<int>(cur.height)-1)); const auto p=cur.pixels[std::size_t(sy)*cur.width+sx]; const float edge=std::exp(-(std::abs(lum(p)-lum(c))/s.luma_threshold)); const float w=(ox==0&&oy==0)?1.0f:edge*s.spatial_strength; sum.r+=p.r*w;sum.g+=p.g*w;sum.b+=p.b*w;sum.a+=p.a*w;wsum+=w;}
  DenoisePixel p{sum.r/wsum,sum.g/wsum,sum.b/wsum,c.a}; const float keep=s.detail_preservation; p.r=p.r*(1-keep)+c.r*keep;p.g=p.g*(1-keep)+c.g*keep;p.b=p.b*(1-keep)+c.b*keep;
  if(prev){const float conf=motion?c01((*motion)[i]):1.0f; const float t=s.temporal_strength*conf*(1.0f-s.motion_sensitivity+conf*s.motion_sensitivity); const auto q=prev->pixels[i]; p.r=p.r*(1-t)+q.r*t;p.g=p.g*(1-t)+q.g*t;p.b=p.b*(1-t)+q.b*t;}
  p.r=c01(p.r);p.g=c01(p.g);p.b=c01(p.b);p.a=c01(p.a);out.pixels[i]=p;partial+=std::abs(p.r-c.r)+std::abs(p.g-c.g)+std::abs(p.b-c.b);}return partial;},[](double a,double b){return a+b;});
 r.status=DenoiseStatus::ready;r.digest=denoise_frame_digest(out);r.average_delta=static_cast<float>(d/(n*3.0));return r;
}
DenoiseResult dispatch_video_denoise_gpu(const DenoiseDispatchPacket& p,const DenoiseDispatch& fn){DenoiseResult r;if(p.backend==DenoiseBackend::cpu||p.width==0||p.height==0||p.current_handle==0||p.output_handle==0||p.command_handle==0||!ok(p.settings))return r;if(!fn){r.status=DenoiseStatus::backend_unavailable;return r;}r.status=fn(p)?DenoiseStatus::ready:DenoiseStatus::dispatch_failed;return r;}
}
extern "C" std::uint32_t digitor_video_denoise_rgba32f(const float* cur,const float* prev,const float* motion,float* out,std::uint32_t w,std::uint32_t h,const DigitorVideoDenoiseSettings* c,std::uint64_t* digest){if(!cur||!out||!c||!digest||w==0||h==0)return 1;const std::size_t n=std::size_t(w)*h;digitor::DenoiseFrame a{w,h,{}},b{w,h,{}};a.pixels.resize(n);if(prev)b.pixels.resize(n);std::vector<float> mv;if(motion)mv.assign(motion,motion+n);digitor::shared_cpu_executor().parallel_for(n,32768,[&](std::size_t begin,std::size_t end){for(std::size_t i=begin;i<end;++i){const auto k=i*4;a.pixels[i]={cur[k],cur[k+1],cur[k+2],cur[k+3]};if(prev)b.pixels[i]={prev[k],prev[k+1],prev[k+2],prev[k+3]};}});digitor::DenoiseSettings s{c->spatial_strength,c->temporal_strength,c->luma_threshold,c->chroma_threshold,c->detail_preservation,c->motion_sensitivity};digitor::DenoiseFrame o;const auto r=digitor::apply_video_denoise_reference(a,prev?&b:nullptr,motion?&mv:nullptr,o,s);if(r.status!=digitor::DenoiseStatus::ready)return 2;digitor::shared_cpu_executor().parallel_for(n,32768,[&](std::size_t begin,std::size_t end){for(std::size_t i=begin;i<end;++i){const auto k=i*4;out[k]=o.pixels[i].r;out[k+1]=o.pixels[i].g;out[k+2]=o.pixels[i].b;out[k+3]=o.pixels[i].a;}});*digest=r.digest;return 0;}
