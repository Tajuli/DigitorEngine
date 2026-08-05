#include "digitor/production_transform_crop.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace digitor { namespace {
float clamp01(float v){return std::clamp(v,0.0f,1.0f);} 
bool valid(const TransformCropFrame& f){return f.width&&f.height&&f.pixels.size()==static_cast<std::size_t>(f.width)*f.height;}
float mirror(float v){v=std::fmod(std::fabs(v),2.0f);return v<=1.0f?v:2.0f-v;}
TransformCropPixel mixp(const TransformCropPixel&a,const TransformCropPixel&b,float t){return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t};}
TransformCropPixel sample(const TransformCropFrame& f,float u,float v,TransformCropFilter filter,TransformCropEdge edge){
  if(edge==TransformCropEdge::transparent&&(u<0||u>1||v<0||v>1)) return {0,0,0,0};
  if(edge==TransformCropEdge::mirror){u=mirror(u);v=mirror(v);}else{u=clamp01(u);v=clamp01(v);} 
  float x=u*static_cast<float>(f.width-1), y=v*static_cast<float>(f.height-1);
  if(filter==TransformCropFilter::nearest){auto xi=static_cast<std::uint32_t>(std::lround(x));auto yi=static_cast<std::uint32_t>(std::lround(y));return f.pixels[static_cast<std::size_t>(yi)*f.width+xi];}
  auto x0=static_cast<std::uint32_t>(x),y0=static_cast<std::uint32_t>(y);auto x1=std::min(x0+1,f.width-1),y1=std::min(y0+1,f.height-1);float tx=x-x0,ty=y-y0;
  auto a=mixp(f.pixels[static_cast<std::size_t>(y0)*f.width+x0],f.pixels[static_cast<std::size_t>(y0)*f.width+x1],tx);
  auto b=mixp(f.pixels[static_cast<std::size_t>(y1)*f.width+x0],f.pixels[static_cast<std::size_t>(y1)*f.width+x1],tx);return mixp(a,b,ty);
}
bool valid_settings(const TransformCropSettings&s){
  for(float v:s.matrix) if(!std::isfinite(v)) return false;
  return s.output_width&&s.output_height&&s.crop_left>=0&&s.crop_top>=0&&s.crop_right<=1&&s.crop_bottom<=1&&s.crop_left<s.crop_right&&s.crop_top<s.crop_bottom;
}
std::uint64_t append(std::uint64_t h,const void*p,std::size_t n){auto*b=static_cast<const unsigned char*>(p);while(n--){h^=*b++;h*=1099511628211ull;}return h;}
}}

std::uint64_t transform_crop_digest(const TransformCropFrame&f) noexcept{std::uint64_t h=1469598103934665603ull;h=append(h,&f.width,sizeof f.width);h=append(h,&f.height,sizeof f.height);if(!f.pixels.empty())h=append(h,f.pixels.data(),f.pixels.size()*sizeof(TransformCropPixel));return h;}

TransformCropResult apply_transform_crop_reference(const TransformCropFrame&in,TransformCropFrame&out,const TransformCropSettings&s){
  TransformCropResult r; if(!valid(in)||!valid_settings(s)) return r; out.width=s.output_width;out.height=s.output_height;out.pixels.resize(static_cast<std::size_t>(out.width)*out.height);
  for(std::uint32_t y=0;y<out.height;++y)for(std::uint32_t x=0;x<out.width;++x){
    float ou=out.width>1?static_cast<float>(x)/(out.width-1):0, ov=out.height>1?static_cast<float>(y)/(out.height-1):0;
    float cu=s.crop_left+ou*(s.crop_right-s.crop_left),cv=s.crop_top+ov*(s.crop_bottom-s.crop_top);
    float px=s.matrix[0]*cu+s.matrix[1]*cv+s.matrix[2],py=s.matrix[3]*cu+s.matrix[4]*cv+s.matrix[5],pw=s.matrix[6]*cu+s.matrix[7]*cv+s.matrix[8];
    out.pixels[static_cast<std::size_t>(y)*out.width+x]=std::fabs(pw)<1e-8f?TransformCropPixel{0,0,0,0}:sample(in,px/pw,py/pw,s.filter,s.edge);
  }
  r.status=TransformCropStatus::ready;r.digest=transform_crop_digest(out);return r;
}

TransformCropResult dispatch_transform_crop_gpu(const TransformCropDispatchPacket&p,const TransformCropDispatch&d){TransformCropResult r;if(p.backend==TransformCropBackend::cpu||!p.input_handle||!p.output_handle||!p.command_handle||!p.input_width||!p.input_height||!valid_settings(p.settings))return r;if(!d){r.status=TransformCropStatus::backend_unavailable;return r;}r.status=d(p)?TransformCropStatus::ready:TransformCropStatus::dispatch_failed;return r;}

} // namespace digitor

extern "C" std::uint32_t digitor_transform_crop_rgba32f(const float*input,std::uint32_t iw,std::uint32_t ih,float*output,const DigitorTransformCropSettings*cs,std::uint64_t*digest){
  if(!input||!output||!cs||!digest||!iw||!ih)return 1;
  digitor::TransformCropFrame in{iw,ih,{}};in.pixels.resize(static_cast<std::size_t>(iw)*ih);for(std::size_t i=0;i<in.pixels.size();++i)in.pixels[i]={input[i*4],input[i*4+1],input[i*4+2],input[i*4+3]};
  digitor::TransformCropSettings s;for(int i=0;i<9;++i)s.matrix[i]=cs->matrix[i];s.crop_left=cs->crop_left;s.crop_top=cs->crop_top;s.crop_right=cs->crop_right;s.crop_bottom=cs->crop_bottom;s.output_width=cs->output_width;s.output_height=cs->output_height;s.filter=static_cast<digitor::TransformCropFilter>(cs->filter);s.edge=static_cast<digitor::TransformCropEdge>(cs->edge);
  digitor::TransformCropFrame out;auto r=digitor::apply_transform_crop_reference(in,out,s);if(r.status!=digitor::TransformCropStatus::ready)return 2;for(std::size_t i=0;i<out.pixels.size();++i){output[i*4]=out.pixels[i].r;output[i*4+1]=out.pixels[i].g;output[i*4+2]=out.pixels[i].b;output[i*4+3]=out.pixels[i].a;}*digest=r.digest;return 0;
}
