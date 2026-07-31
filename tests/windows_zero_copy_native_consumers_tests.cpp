#include "digitor/windows_zero_copy_native_consumers.hpp"

#include <atomic>
#include <cassert>
#include <fstream>

int main(){
  using namespace digitor;
  const char* evidence="windows-zero-copy-native-consumers-evidence.json";
  {std::ofstream f(evidence);f<<"{\"production_ready\":true,\"adapter\":\"a\",\"driver\":\"d\",\"commit\":\"c\"}";}
  std::atomic_int decodes{},leases{},releases{},previews{},encodes{};
  auto native=std::make_shared<int>(1);
  auto ready=std::make_shared<std::atomic_bool>(true);
  auto decode=[&](std::int64_t ts,ProcessedGpuFramePtr& out){++decodes;out=std::make_shared<ProcessedGpuFrame>((void*)0x1,DIGITOR_RENDERER_D3D12,GpuFrameMetadata{4,4,DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT,GpuFrameAlpha::straight,ts,"linear-rgba16f"},std::uint64_t(ts+100),native,ready,false);return DIGITOR_RESULT_OK;};
  auto lease=[&](const ProcessedGpuFramePtr& frame,WindowsNativeConsumerKind kind,WindowsD3D12FrameLease& out){++leases;out.resource=(void*)0x2;out.width=4;out.height=4;out.format=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;out.timestamp_us=frame->metadata().timestamp;out.frame_identity=frame->identity();out.consumer=kind;out.release=[&]{++releases;};return DIGITOR_RESULT_OK;};
  WindowsZeroCopyNativeBinding binding;binding.decode=decode;binding.lease_provider=lease;binding.preview.command_queue=(void*)0x3;binding.preview.swapchain=(void*)0x4;binding.encoder.width=4;binding.encoder.height=4;binding.encoder_submit=[&](const WindowsD3D12FrameLease& l){assert(l.resource);++encodes;return DIGITOR_RESULT_OK;};
  WindowsZeroCopyRuntimeConfig config;config.enabled=true;config.strict_gpu_first=true;config.evidence_path=evidence;config.adapter_luid="a";config.driver_version="d";config.engine_commit="c";
  WindowsZeroCopyNativePipeline pipeline(config,std::move(binding));assert(pipeline.initialize()==DIGITOR_RESULT_OK);
  assert(pipeline.preview(10)==DIGITOR_RESULT_OK);++previews;
  assert(pipeline.export_frame(10)==DIGITOR_RESULT_OK);
  assert(decodes==1);assert(leases==2);assert(releases==2);assert(previews==1);assert(encodes==1);
  assert(pipeline.runtime_telemetry().cpu_fallback_frames==0);
  assert(pipeline.preview_telemetry().cpu_copies==0);
  assert(pipeline.encoder_telemetry().cpu_copies==0);
  std::remove(evidence);return 0;
}
