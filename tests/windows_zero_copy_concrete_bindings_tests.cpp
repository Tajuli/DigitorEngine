#include "digitor/windows_zero_copy_concrete_bindings.hpp"

#include <atomic>
#include <cassert>

int main(){
  using namespace digitor;
  WindowsD3D12LeaseRegistry registry;
  auto lifetime=std::make_shared<int>(7);
  WindowsD3D12ProducedFrame produced;
  produced.rgba16f_resource=reinterpret_cast<void*>(0x1000);
  produced.width=1920;produced.height=1080;produced.timestamp_us=40000;produced.frame_identity=99;produced.lifetime=lifetime;
  assert(registry.publish(std::move(produced))==DIGITOR_RESULT_OK);
  auto frame=std::make_shared<ProcessedGpuFrame>(reinterpret_cast<void*>(0x2000),DIGITOR_RENDERER_D3D12,
    GpuFrameMetadata{1920,1080,DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT,GpuFrameAlpha::straight,40000,"linear-rgba16f"},99,
    std::make_shared<int>(1),std::make_shared<std::atomic_bool>(true),false);
  WindowsD3D12FrameLease preview;
  assert(registry.provider()(frame,WindowsNativeConsumerKind::preview_swapchain,preview)==DIGITOR_RESULT_OK);
  assert(preview.resource==reinterpret_cast<void*>(0x1000));
  assert(preview.timestamp_us==40000&&preview.frame_identity==99);
  WindowsD3D12FrameLease encoder;
  assert(registry.provider()(frame,WindowsNativeConsumerKind::hardware_encoder,encoder)==DIGITOR_RESULT_OK);
  assert(encoder.resource==preview.resource);
  registry.retire(99);
  WindowsD3D12FrameLease missing;
  assert(registry.provider()(frame,WindowsNativeConsumerKind::preview_swapchain,missing)==DIGITOR_RESULT_NOT_INITIALIZED);
  return 0;
}
