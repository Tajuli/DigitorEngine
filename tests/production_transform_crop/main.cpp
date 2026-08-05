#include "digitor/production_transform_crop.hpp"
#include <cstdint>
#include <vector>

int main(){using namespace digitor;
  TransformCropFrame input{2,2,{{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,1,1}}};
  TransformCropSettings s; s.output_width=2;s.output_height=2;
  TransformCropFrame preview,export_frame;
  auto a=apply_transform_crop_reference(input,preview,s);auto b=apply_transform_crop_reference(input,export_frame,s);
  if(a.status!=TransformCropStatus::ready||a.digest!=b.digest)return 1;
  s.crop_left=0.5f;s.output_width=1;TransformCropFrame cropped;if(apply_transform_crop_reference(input,cropped,s).status!=TransformCropStatus::ready)return 2;
  s.matrix[2]=0.25f;s.edge=TransformCropEdge::transparent;TransformCropFrame moved;if(apply_transform_crop_reference(input,moved,s).status!=TransformCropStatus::ready)return 3;
  TransformCropDispatchPacket p; p.backend=TransformCropBackend::vulkan;p.input_handle=1;p.output_handle=2;p.command_handle=3;p.input_width=2;p.input_height=2;p.settings=s;
  if(dispatch_transform_crop_gpu(p,{}).status!=TransformCropStatus::backend_unavailable)return 4;
  if(dispatch_transform_crop_gpu(p,[](const auto&){return true;}).status!=TransformCropStatus::ready)return 5;
  p.command_handle=0;if(dispatch_transform_crop_gpu(p,[](const auto&){return true;}).status!=TransformCropStatus::invalid)return 6;
  std::vector<float> packed(16),out(4);for(std::size_t i=0;i<input.pixels.size();++i){packed[i*4]=input.pixels[i].r;packed[i*4+1]=input.pixels[i].g;packed[i*4+2]=input.pixels[i].b;packed[i*4+3]=input.pixels[i].a;}
  DigitorTransformCropSettings cs{};cs.matrix[0]=cs.matrix[4]=cs.matrix[8]=1;cs.crop_right=cs.crop_bottom=1;cs.output_width=cs.output_height=1;cs.filter=1;std::uint64_t d{};
  if(digitor_transform_crop_rgba32f(packed.data(),2,2,out.data(),&cs,&d)!=0||d==0)return 7;
  return 0;}
