#include "digitor/windows_zero_copy_frame_broker.hpp"
#include "digitor/windows_zero_copy_runtime.hpp"
#include <atomic>
#include <cassert>
#include <fstream>

int main(){
  using namespace digitor;
  const char* evidence="windows-zero-copy-runtime-test.json";
  {std::ofstream f(evidence);f<<"{\"production_ready\":true,\"adapter\":\"A1\",\"driver\":\"D1\",\"commit\":\"C1\"}";}
  std::atomic_uint64_t decodes{};std::atomic_uint64_t previews{};std::atomic_uint64_t exports{};
  auto decode=[&](std::int64_t ts,ProcessedGpuFramePtr& out){++decodes;GpuFrameMetadata m{};m.width=2;m.height=2;m.format=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;m.timestamp=ts;
    out=std::make_shared<ProcessedGpuFrame>((void*)1,DIGITOR_RENDERER_D3D12,m,static_cast<std::uint64_t>(ts),std::make_shared<int>(1),std::make_shared<std::atomic_bool>(true),false);return DIGITOR_RESULT_OK;};
  WindowsZeroCopyFrameBroker broker(decode,2);
  auto preview=[&](const ProcessedGpuFramePtr& f){++previews;return f&&f->ready()?DIGITOR_RESULT_OK:DIGITOR_RESULT_INTERNAL_ERROR;};
  auto export_frame=[&](const ProcessedGpuFramePtr& f){++exports;return f&&f->ready()?DIGITOR_RESULT_OK:DIGITOR_RESULT_INTERNAL_ERROR;};
  ProcessedGpuFramePtr a,b;assert(broker.acquire(100,a)==DIGITOR_RESULT_OK);assert(broker.acquire(100,b)==DIGITOR_RESULT_OK);assert(a==b&&decodes==1);
  assert(broker.deliver_preview(200,preview)==DIGITOR_RESULT_OK);assert(broker.deliver_export(200,export_frame)==DIGITOR_RESULT_OK);assert(decodes==2&&previews==1&&exports==1);
  WindowsZeroCopyRuntimeConfig cfg;cfg.enabled=true;cfg.strict_gpu_first=true;cfg.evidence_path=evidence;cfg.adapter_luid="A1";cfg.driver_version="D1";cfg.engine_commit="C1";
  WindowsZeroCopyRuntime runtime(cfg,decode,preview,export_frame);assert(runtime.initialize()==DIGITOR_RESULT_OK);assert(runtime.production_active());
  assert(runtime.render_preview(300)==DIGITOR_RESULT_OK);assert(runtime.render_export(301)==DIGITOR_RESULT_OK);auto t=runtime.telemetry();assert(t.zero_copy_frames==2&&t.cpu_fallback_frames==0);
  cfg.adapter_luid="wrong";WindowsZeroCopyRuntime rejected(cfg,decode,preview,export_frame);assert(rejected.initialize()!=DIGITOR_RESULT_OK&&!rejected.production_active());
  std::remove(evidence);return 0;
}
