#include "digitor/production_video_denoise.hpp"
#include <iostream>
#include <vector>
using namespace digitor;
int main(){
 DenoiseFrame current{3,1,{{0.1f,0.1f,0.1f,1},{0.9f,0.9f,0.9f,1},{0.1f,0.1f,0.1f,1}}};
 DenoiseFrame previous{3,1,{{0.1f,0.1f,0.1f,1},{0.8f,0.8f,0.8f,1},{0.1f,0.1f,0.1f,1}}};
 std::vector<float> motion{1,0,1}; DenoiseFrame preview,exported; DenoiseSettings s;
 const auto a=apply_video_denoise_reference(current,&previous,&motion,preview,s);
 const auto b=apply_video_denoise_reference(current,&previous,&motion,exported,s);
 if(a.status!=DenoiseStatus::ready||a.digest!=b.digest||preview.pixels.size()!=3)return 1;
 for(auto backend:{DenoiseBackend::vulkan,DenoiseBackend::d3d12,DenoiseBackend::metal,DenoiseBackend::gles}){
  DenoiseDispatchPacket p; p.backend=backend;p.width=3;p.height=1;p.current_handle=1;p.previous_handle=2;p.motion_handle=3;p.output_handle=4;p.command_handle=5;
  if(dispatch_video_denoise_gpu(p,[](const DenoiseDispatchPacket&){return true;}).status!=DenoiseStatus::ready)return 2;
 }
 float in[12]={.1f,.1f,.1f,1,.9f,.9f,.9f,1,.1f,.1f,.1f,1},out[12]{}; DigitorVideoDenoiseSettings c{.35f,.45f,.08f,.1f,.65f,.7f}; std::uint64_t digest{};
 if(digitor_video_denoise_rgba32f(in,nullptr,nullptr,out,3,1,&c,&digest)!=0||digest==0)return 3;
 std::cout<<"VIDEO_DENOISE=1\nPREVIEW_EXPORT_PARITY=1\nGPU_BACKENDS=4\nC_ABI=1\n"; return 0;
}
