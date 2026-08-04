#include "digitor/spatial_compositor_c.h"
#include "digitor/spatial_compositor.hpp"
#include <algorithm>
#include <exception>

using namespace digitor;
namespace {
VisualBackend backend_from_int(int v){switch(v){case 0:return VisualBackend::vulkan;case 1:return VisualBackend::d3d12;case 2:return VisualBackend::metal;case 3:return VisualBackend::gles;default:return VisualBackend::cpu;}}
}
extern "C" int digitor_render_spatial_rgba32f(const DigitorPixelF* input,uint32_t iw,uint32_t ih,DigitorPixelF* output,uint32_t ow,uint32_t oh,const DigitorSpatialParams* p,int backend,int gpu_available,int allow_cpu_fallback){
  if(!input||!output||!p||!iw||!ih||!ow||!oh)return DIGITOR_SPATIAL_INVALID_ARGUMENT;
  try{FrameF in{iw,ih,std::vector<PixelF>(std::size_t(iw)*ih)};for(std::size_t i=0;i<in.pixels.size();++i)in.pixels[i]={input[i].r,input[i].g,input[i].b,input[i].a};
    SpatialSettings s;s.transform.position={p->position_x,p->position_y};s.transform.scale={p->scale_x,p->scale_y};s.transform.anchor={p->anchor_x,p->anchor_y};s.transform.rotation_degrees=p->rotation_degrees;s.transform.opacity=p->opacity;s.transform.flip_x=p->flip_x!=0;s.transform.flip_y=p->flip_y!=0;s.crop.normalized={p->crop_left,p->crop_top,p->crop_right,p->crop_bottom};s.sampling=p->bilinear?SamplingMode::bilinear:SamplingMode::nearest;
    auto r=render_spatial(in,ow,oh,s,{backend_from_int(backend),gpu_available!=0,allow_cpu_fallback!=0,false});if(!r.frame.valid())return DIGITOR_SPATIAL_GPU_UNAVAILABLE;for(std::size_t i=0;i<r.frame.pixels.size();++i)output[i]={r.frame.pixels[i].r,r.frame.pixels[i].g,r.frame.pixels[i].b,r.frame.pixels[i].a};return DIGITOR_SPATIAL_OK;
  }catch(...){return DIGITOR_SPATIAL_INTERNAL_ERROR;}}
extern "C" int digitor_apply_chroma_key_rgba32f(DigitorPixelF* pixels,uint32_t w,uint32_t h,const DigitorChromaParams* p){if(!pixels||!p||!w||!h)return DIGITOR_SPATIAL_INVALID_ARGUMENT;try{FrameF f{w,h,std::vector<PixelF>(std::size_t(w)*h)};for(std::size_t i=0;i<f.pixels.size();++i)f.pixels[i]={pixels[i].r,pixels[i].g,pixels[i].b,pixels[i].a};ChromaKeyState s;s.key_r=p->r;s.key_g=p->g;s.key_b=p->b;s.similarity=p->similarity;s.softness=p->softness;s.spill=p->spill;s.enabled=p->enabled!=0;apply_chroma_key(f,s);for(std::size_t i=0;i<f.pixels.size();++i)pixels[i]={f.pixels[i].r,f.pixels[i].g,f.pixels[i].b,f.pixels[i].a};return DIGITOR_SPATIAL_OK;}catch(...){return DIGITOR_SPATIAL_INTERNAL_ERROR;}}
