#include "digitor/production_chroma_key.hpp"
#include <iostream>
using namespace digitor;
int main() {
  ChromaFrame input{2,1,{{0.0f,1.0f,0.0f,1.0f},{1.0f,0.0f,0.0f,1.0f}}}, preview, export_frame;
  ChromaKeySettings settings; settings.similarity=0.25f; settings.softness=0.1f; settings.despill=1.0f;
  const auto a=apply_chroma_key_reference(input,preview,settings);
  const auto b=apply_chroma_key_reference(input,export_frame,settings);
  if(a.status!=ChromaStatus::ready||b.status!=ChromaStatus::ready)return 1;
  if(a.digest!=b.digest||preview.pixels[0].a>=0.01f||preview.pixels[1].a<=0.99f)return 2;
  if(preview.pixels[0].g!=0.0f)return 3;
  for(auto backend:{ChromaBackend::vulkan,ChromaBackend::d3d12,ChromaBackend::metal,ChromaBackend::gles}){
    ChromaDispatchPacket p; p.backend=backend;p.input_handle=1;p.output_handle=2;p.command_handle=3;p.width=2;p.height=1;p.settings=settings;
    bool called=false;const auto r=dispatch_chroma_key_gpu(p,[&](const ChromaDispatchPacket& q){called=true;return !chroma_shader_source(q.backend).empty();});
    if(r.status!=ChromaStatus::ready||!called)return 4;
  }
  ChromaDispatchPacket bad;bad.backend=ChromaBackend::vulkan;bad.width=1;bad.height=1;
  if(dispatch_chroma_key_gpu(bad,{}).status!=ChromaStatus::invalid_argument)return 5;
  float in[8]={0,1,0,1,1,0,0,1},out[8]{};std::uint64_t digest{};
  DigitorChromaKeySettings c{};c.key_mode=0;c.similarity=.25f;c.softness=.1f;c.despill=1;c.spill_balance=.5f;
  if(digitor_chroma_key_rgba32f(in,out,2,1,&c,&digest)!=0||digest!=a.digest)return 6;
  std::cout<<"GREEN_SCREEN_REMOVAL=1\nDESPILL=1\nPREVIEW_EXPORT_PARITY=1\nGPU_BACKENDS=4\nFFI=1\n";
  return 0;
}
