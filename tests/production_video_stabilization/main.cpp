#include "digitor/production_video_stabilization.hpp"

#include <cmath>
#include <iostream>
#include <vector>

int main() {
  using namespace digitor;
  const std::vector<MotionSample> samples{{0.0,0,0,0,1},{0.033,8,-4,0.08f,1},{0.066,-5,3,-0.06f,0.9f},{0.099,2,-1,0.02f,1}};
  StabilizationSettings settings; settings.lock_horizon=true; settings.rolling_shutter_correction=true;
  const auto preview=build_stabilization_plan(samples,settings);
  const auto export_plan=build_stabilization_plan(samples,settings);
  if(preview.transforms.size()!=samples.size()||preview.digest==0||preview.digest!=export_plan.digest)return 1;
  if(preview.transforms[1].zoom<1.0f||preview.transforms[1].zoom>settings.max_zoom)return 2;
  if(std::abs(preview.transforms[1].rotation)>=std::abs(samples[1].rotation)+0.001f)return 3;
  for(auto backend:{StabilizationBackend::vulkan,StabilizationBackend::d3d12,StabilizationBackend::metal,StabilizationBackend::gles}){
    StabilizationDispatchPacket packet;packet.backend=backend;packet.input_handle=1;packet.output_handle=2;packet.command_handle=3;packet.width=1920;packet.height=1080;packet.transform=preview.transforms[1];
    const auto result=dispatch_stabilized_frame(packet,[](const StabilizationDispatchPacket&){return true;});
    if(result.status!=StabilizationStatus::ready||result.digest==0)return 4;
  }
  StabilizationDispatchPacket invalid;invalid.backend=StabilizationBackend::vulkan;
  if(dispatch_stabilized_frame(invalid,{}).status!=StabilizationStatus::invalid)return 5;
  DigitorMotionSample c_samples[2]{{0,0,0,0,1},{0.033,3,-2,0.04f,1}};
  DigitorStabilizationSettings c_settings{0.75f,0.85f,1.15f,1,1};
  DigitorStabilizationTransform out[2]{};std::uint64_t digest{};
  if(digitor_build_stabilization_plan(c_samples,2,&c_settings,out,&digest)!=0||digest==0)return 6;
  std::cout<<"STABILIZATION_PLAN=1\nPREVIEW_EXPORT_PARITY=1\nGPU_BACKENDS=4\nC_ABI=1\n";
  return 0;
}
