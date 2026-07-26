#include "digitor/video_texture.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace digitor { namespace {
float clamp(float x){return std::clamp(x,0.0f,1.0f);} Color yuv(float y,float u,float v,ColorRange range){
 if(range!=ColorRange::full){y=(y*255-16)/219;u=(u*255-128)/224;v=(v*255-128)/224;}else{u-=.5f;v-=.5f;}
 return linearize_srgb({clamp(y+1.5748f*v),clamp(y-.187324f*u-.468124f*v),clamp(y+1.8556f*u),1});}
void plane(const DecodedImage&i,unsigned n,std::size_t rows,std::size_t rowbytes){if(i.planes[n].stride<rowbytes||i.planes[n].bytes.size()<i.planes[n].stride*rows)throw std::invalid_argument("invalid image plane");}
}
std::vector<Color> convert_to_linear_rgba(const DecodedImage&i){if(!i.width||!i.height)throw std::invalid_argument("empty image");std::vector<Color> out(size_t(i.width)*i.height);
 if(i.format==PixelFormat::rgba8||i.format==PixelFormat::bgra8){plane(i,0,i.height,size_t(i.width)*4);for(uint32_t y=0;y<i.height;y++)for(uint32_t x=0;x<i.width;x++){auto*p=i.planes[0].bytes.data()+y*i.planes[0].stride+x*4;out[y*i.width+x]=linearize_srgb(i.format==PixelFormat::rgba8?Color{p[0]/255.f,p[1]/255.f,p[2]/255.f,p[3]/255.f}:Color{p[2]/255.f,p[1]/255.f,p[0]/255.f,p[3]/255.f});}}
 else if(i.format==PixelFormat::nv12){plane(i,0,i.height,i.width);plane(i,1,(i.height+1)/2,((i.width+1)/2)*2);for(uint32_t y=0;y<i.height;y++)for(uint32_t x=0;x<i.width;x++){auto Y=i.planes[0].bytes[y*i.planes[0].stride+x]/255.f;auto uv=i.planes[1].bytes.data()+(y/2)*i.planes[1].stride+(x/2)*2;out[y*i.width+x]=yuv(Y,uv[0]/255.f,uv[1]/255.f,i.range);}}
 else if(i.format==PixelFormat::yuv420p){plane(i,0,i.height,i.width);plane(i,1,(i.height+1)/2,(i.width+1)/2);plane(i,2,(i.height+1)/2,(i.width+1)/2);for(uint32_t y=0;y<i.height;y++)for(uint32_t x=0;x<i.width;x++)out[y*i.width+x]=yuv(i.planes[0].bytes[y*i.planes[0].stride+x]/255.f,i.planes[1].bytes[(y/2)*i.planes[1].stride+x/2]/255.f,i.planes[2].bytes[(y/2)*i.planes[2].stride+x/2]/255.f,i.range);}
 else throw std::invalid_argument("unsupported upload format");return out;}
NativeVideoTexture upload_video_texture(DigitorRendererBackend b,const DecodedImage&i){if(b!=DIGITOR_RENDERER_VULKAN&&b!=DIGITOR_RENDERER_D3D12&&b!=DIGITOR_RENDERER_METAL&&b!=DIGITOR_RENDERER_OPENGL_ES&&b!=DIGITOR_RENDERER_CPU)throw std::invalid_argument("unsupported backend");return{b,i.width,i.height,i.format,convert_to_linear_rgba(i)};}
PixelValidation validate_pixels(std::span<const Color>a,std::span<const Color>b,float t){if(a.size()!=b.size()||t<0)throw std::invalid_argument("invalid pixel validation");PixelValidation r{};for(size_t i=0;i<a.size();i++){float e=std::max({std::abs(a[i].r-b[i].r),std::abs(a[i].g-b[i].g),std::abs(a[i].b-b[i].b),std::abs(a[i].a-b[i].a)});r.maximum_error=std::max(r.maximum_error,e);r.mean_error+=e;if(e>t)r.failing_pixels++;}if(!a.empty())r.mean_error/=a.size();r.passed=!r.failing_pixels;return r;}
}
