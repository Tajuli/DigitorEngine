#include "digitor/spatial_compositor.hpp"
#include "digitor/spatial_compositor_c.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace digitor;
int main(){
  FrameF fg{2,2,{{0,1,0,1},{1,0,0,1},{0,0,1,1},{1,1,1,1}}};FrameF bg{4,4,std::vector<PixelF>(16,{0.1f,0.1f,0.1f,1})};
  LayerSettings layer;layer.spatial.transform.position={0,0};layer.spatial.transform.scale={1,1};layer.chroma_key.enabled=true;layer.chroma_key.similarity=.25;layer.chroma_key.softness=.2;
  StabilizationState stab;stab.enabled=true;stab.strength=1;stab.crop_ratio=.05;std::vector<MotionSample> motion{{0,0,0,0,1},{10,.1,0,2,1}};
  RenderPolicy cpu{VisualBackend::cpu,false,true,true};auto preview=render_layer(fg,bg,layer,stab,motion,5,cpu);auto export_frame=render_layer(fg,bg,layer,stab,motion,5,{VisualBackend::cpu,false,true,false});
  assert(preview.frame.valid());assert(stable_frame_digest(preview.frame)==stable_frame_digest(export_frame.frame));
  auto denied=render_spatial(fg,4,4,layer.spatial,{VisualBackend::vulkan,false,false,true});assert(!denied.frame.valid());
  AnimatedScalar a{0,{{0,0},{10,100}}};assert(std::abs(evaluate(a,5)-50)<1e-9);
  FrameF keyed=fg;apply_chroma_key(keyed,layer.chroma_key);assert(keyed.pixels[0].a<.01f);assert(keyed.pixels[1].a>.9f);
  FrameF normal=bg;FrameF overlay{4,4,std::vector<PixelF>(16,{1,0,0,.5f})};composite(normal,overlay,BlendMode::normal,1);assert(normal.pixels[0].r>.5f);
  DigitorPixelF input[4]={{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,1,1}}, output[16]{};DigitorSpatialParams p{};p.scale_x=p.scale_y=1;p.anchor_x=p.anchor_y=.5;p.opacity=1;p.crop_right=p.crop_bottom=1;p.bilinear=1;
  assert(digitor_render_spatial_rgba32f(input,2,2,output,4,4,&p,4,0,1)==DIGITOR_SPATIAL_OK);
  assert(digitor_render_spatial_rgba32f(input,2,2,output,4,4,&p,0,0,0)==DIGITOR_SPATIAL_GPU_UNAVAILABLE);
  std::cout<<"SPATIAL_TRANSFORM_CROP=1\nCHROMA_KEY_DESPILL=1\nALPHA_COMPOSITOR=1\nSTABILIZATION_TRACK_APPLICATION=1\nANIMATED_PROPERTIES=1\nC_ABI=1\nPREVIEW_EXPORT_PARITY=1\nGPU_FALLBACK_POLICY=1\n";
}
