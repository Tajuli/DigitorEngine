#include "digitor/windows_zero_copy_runtime.hpp"

#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

namespace digitor {
namespace {
bool contains_json_value(const std::string& json,const std::string& key,const std::string& value){
  return json.find("\""+key+"\"")!=std::string::npos && json.find(value)!=std::string::npos;
}
bool evidence_accepts(const WindowsZeroCopyRuntimeConfig& c,std::string& diagnostic){
  if(!c.enabled){diagnostic="Windows zero-copy runtime is disabled";return false;}
  if(!c.strict_gpu_first){diagnostic="production zero-copy requires strict GPU-first mode";return false;}
  if(c.evidence_path.empty()||c.adapter_luid.empty()||c.driver_version.empty()||c.engine_commit.empty()){
    diagnostic="qualification evidence and hardware identity are required";return false;
  }
  std::ifstream f(c.evidence_path,std::ios::binary);if(!f){diagnostic="qualification evidence cannot be opened";return false;}
  std::ostringstream s;s<<f.rdbuf();const auto json=s.str();
  if(!contains_json_value(json,"production_ready","true")) {diagnostic="qualification evidence is not production-ready";return false;}
  if(json.find(c.adapter_luid)==std::string::npos){diagnostic="qualification adapter does not match runtime adapter";return false;}
  if(json.find(c.driver_version)==std::string::npos){diagnostic="qualification driver does not match runtime driver";return false;}
  if(json.find(c.engine_commit)==std::string::npos){diagnostic="qualification commit does not match runtime engine";return false;}
  diagnostic="qualification evidence accepted";return true;
}
}

struct WindowsZeroCopyRuntime::Impl {
  WindowsZeroCopyRuntimeConfig config;
  WindowsZeroCopyDecodeCallback decode;
  WindowsZeroCopyFrameConsumer preview;
  WindowsZeroCopyFrameConsumer export_frame;
  mutable std::mutex mutex;
  WindowsZeroCopyRuntimeTelemetry telemetry;

  DigitorResult fail(WindowsZeroCopyFailureClass kind,const char* text,DigitorResult result){
    std::scoped_lock lock(mutex);telemetry.last_failure=kind;telemetry.diagnostic=text?text:"zero-copy failure";
    ++telemetry.frames_rejected;++telemetry.consecutive_failures;
    if(kind==WindowsZeroCopyFailureClass::device_removed){++telemetry.device_loss_events;telemetry.state=WindowsZeroCopyRuntimeState::device_lost;}
    else if(config.allow_session_quarantine&&telemetry.consecutive_failures>=config.consecutive_failure_limit)telemetry.state=WindowsZeroCopyRuntimeState::quarantined;
    return result;
  }
  DigitorResult render(std::int64_t timestamp,const WindowsZeroCopyFrameConsumer& consume){
    {
      std::scoped_lock lock(mutex);++telemetry.frames_requested;
      if(telemetry.state!=WindowsZeroCopyRuntimeState::active)return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    ProcessedGpuFramePtr frame;const auto begin=std::chrono::steady_clock::now();
    auto result=decode?decode(timestamp,frame):DIGITOR_RESULT_NOT_INITIALIZED;
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
    if(elapsed>config.frame_timeout_ms)return fail(WindowsZeroCopyFailureClass::timeout,"zero-copy frame exceeded runtime deadline",DIGITOR_RESULT_BACKEND_UNAVAILABLE);
    if(result!=DIGITOR_RESULT_OK||!frame)return fail(WindowsZeroCopyFailureClass::decoder_failure,"zero-copy decoder did not return a GPU frame",result==DIGITOR_RESULT_OK?DIGITOR_RESULT_INTERNAL_ERROR:result);
    if(frame->metadata().timestamp!=timestamp){
      {std::scoped_lock lock(mutex);++telemetry.timestamp_mismatches;}
      return fail(WindowsZeroCopyFailureClass::timestamp_mismatch,"zero-copy timestamp mismatch",DIGITOR_RESULT_INTERNAL_ERROR);
    }
    if(frame->backend()!=DIGITOR_RENDERER_D3D12||!frame->ready())return fail(WindowsZeroCopyFailureClass::integrity_failure,"zero-copy frame is not a ready D3D12 resource",DIGITOR_RESULT_INTERNAL_ERROR);
    result=consume?consume(frame):DIGITOR_RESULT_NOT_INITIALIZED;
    if(result!=DIGITOR_RESULT_OK)return fail(WindowsZeroCopyFailureClass::integrity_failure,"GPU frame consumer rejected zero-copy output",result);
    std::scoped_lock lock(mutex);++telemetry.frames_completed;++telemetry.zero_copy_frames;telemetry.consecutive_failures=0;telemetry.last_failure=WindowsZeroCopyFailureClass::none;telemetry.diagnostic="zero-copy frame completed";return DIGITOR_RESULT_OK;
  }
};

WindowsZeroCopyRuntime::WindowsZeroCopyRuntime(WindowsZeroCopyRuntimeConfig c,WindowsZeroCopyDecodeCallback d,WindowsZeroCopyFrameConsumer p,WindowsZeroCopyFrameConsumer e):impl_(std::make_unique<Impl>()){
  impl_->config=std::move(c);impl_->decode=std::move(d);impl_->preview=std::move(p);impl_->export_frame=std::move(e);
}
WindowsZeroCopyRuntime::~WindowsZeroCopyRuntime()=default;
DigitorResult WindowsZeroCopyRuntime::initialize() noexcept {
  try{std::string diagnostic;const bool accepted=evidence_accepts(impl_->config,diagnostic);std::scoped_lock lock(impl_->mutex);impl_->telemetry.diagnostic=diagnostic;
    if(!impl_->config.enabled){impl_->telemetry.state=WindowsZeroCopyRuntimeState::disabled;return DIGITOR_RESULT_UNSUPPORTED;}
    if(!accepted){impl_->telemetry.state=WindowsZeroCopyRuntimeState::evidence_rejected;impl_->telemetry.last_failure=WindowsZeroCopyFailureClass::evidence_mismatch;return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
    if(!impl_->decode||!impl_->preview||!impl_->export_frame){impl_->telemetry.state=WindowsZeroCopyRuntimeState::evidence_rejected;impl_->telemetry.diagnostic="decode, preview, and export callbacks are required";return DIGITOR_RESULT_INVALID_ARGUMENT;}
    impl_->telemetry.state=WindowsZeroCopyRuntimeState::active;return DIGITOR_RESULT_OK;
  }catch(...){return DIGITOR_RESULT_INTERNAL_ERROR;}
}
DigitorResult WindowsZeroCopyRuntime::render_preview(std::int64_t t) noexcept {try{return impl_->render(t,impl_->preview);}catch(...){return DIGITOR_RESULT_INTERNAL_ERROR;}}
DigitorResult WindowsZeroCopyRuntime::render_export(std::int64_t t) noexcept {try{return impl_->render(t,impl_->export_frame);}catch(...){return DIGITOR_RESULT_INTERNAL_ERROR;}}
DigitorResult WindowsZeroCopyRuntime::reset_quarantine() noexcept {std::scoped_lock lock(impl_->mutex);if(impl_->telemetry.state!=WindowsZeroCopyRuntimeState::quarantined)return DIGITOR_RESULT_INVALID_ARGUMENT;impl_->telemetry.state=WindowsZeroCopyRuntimeState::active;impl_->telemetry.consecutive_failures=0;impl_->telemetry.last_failure=WindowsZeroCopyFailureClass::none;impl_->telemetry.diagnostic="session quarantine reset";return DIGITOR_RESULT_OK;}
WindowsZeroCopyRuntimeTelemetry WindowsZeroCopyRuntime::telemetry() const {std::scoped_lock lock(impl_->mutex);return impl_->telemetry;}
bool WindowsZeroCopyRuntime::production_active() const noexcept {std::scoped_lock lock(impl_->mutex);return impl_->telemetry.state==WindowsZeroCopyRuntimeState::active;}
} // namespace digitor
