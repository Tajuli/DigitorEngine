#include "digitor/windows_zero_copy_native_consumers.hpp"

#include <mutex>
#include <utility>

namespace digitor {

WindowsD3D12FrameLease::WindowsD3D12FrameLease(WindowsD3D12FrameLease&& other) noexcept {
  *this = std::move(other);
}
WindowsD3D12FrameLease& WindowsD3D12FrameLease::operator=(WindowsD3D12FrameLease&& other) noexcept {
  if(this==&other) return *this; reset();
  resource=other.resource;producer_fence=other.producer_fence;
  producer_fence_value=other.producer_fence_value;width=other.width;height=other.height;
  format=other.format;timestamp_us=other.timestamp_us;frame_identity=other.frame_identity;
  consumer=other.consumer;release=std::move(other.release);
  other.resource=nullptr;other.producer_fence=nullptr;other.producer_fence_value=0;
  return *this;
}
WindowsD3D12FrameLease::~WindowsD3D12FrameLease(){reset();}
void WindowsD3D12FrameLease::reset() noexcept {
  if(release){try{release();}catch(...){}} release={};resource=nullptr;producer_fence=nullptr;
}

struct WindowsD3D12PreviewConsumer::Impl {
  WindowsPreviewConsumerConfig config; WindowsD3D12LeaseProvider lease_provider;
  mutable std::mutex mutex; WindowsNativeConsumerTelemetry telemetry;
};
WindowsD3D12PreviewConsumer::WindowsD3D12PreviewConsumer(WindowsPreviewConsumerConfig c,WindowsD3D12LeaseProvider p):impl_(std::make_unique<Impl>()) {impl_->config=c;impl_->lease_provider=std::move(p);} 
WindowsD3D12PreviewConsumer::~WindowsD3D12PreviewConsumer()=default;
DigitorResult WindowsD3D12PreviewConsumer::consume(const ProcessedGpuFramePtr& frame) noexcept {
  if(!frame||frame->backend()!=DIGITOR_RENDERER_D3D12||!frame->ready())return DIGITOR_RESULT_INVALID_ARGUMENT;
  if(!impl_->config.command_queue||!impl_->config.swapchain||!impl_->lease_provider)return DIGITOR_RESULT_NOT_INITIALIZED;
  WindowsD3D12FrameLease lease;auto r=impl_->lease_provider(frame,WindowsNativeConsumerKind::preview_swapchain,lease);
  if(r!=DIGITOR_RESULT_OK||!lease){std::scoped_lock l(impl_->mutex);++impl_->telemetry.lease_failures;impl_->telemetry.diagnostic="preview lease failed";return r==DIGITOR_RESULT_OK?DIGITOR_RESULT_INTERNAL_ERROR:r;}
  if(lease.timestamp_us!=frame->metadata().timestamp){std::scoped_lock l(impl_->mutex);++impl_->telemetry.timestamp_mismatches;return DIGITOR_RESULT_INTERNAL_ERROR;}
  if(lease.frame_identity!=frame->identity()){std::scoped_lock l(impl_->mutex);++impl_->telemetry.identity_mismatches;return DIGITOR_RESULT_INTERNAL_ERROR;}
  if(lease.format!=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT&&lease.format!=DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT)return DIGITOR_RESULT_UNSUPPORTED;
  std::scoped_lock l(impl_->mutex);++impl_->telemetry.preview_frames;impl_->telemetry.diagnostic="preview consumed shared GPU frame";return DIGITOR_RESULT_OK;
}
WindowsZeroCopyFrameConsumer WindowsD3D12PreviewConsumer::callback(){return [this](const ProcessedGpuFramePtr& f){return consume(f);};}
WindowsNativeConsumerTelemetry WindowsD3D12PreviewConsumer::telemetry() const {std::scoped_lock l(impl_->mutex);return impl_->telemetry;}

struct WindowsHardwareEncoderConsumer::Impl {
  WindowsEncoderConsumerConfig config; WindowsD3D12LeaseProvider lease_provider; SubmitCallback submit;
  mutable std::mutex mutex; WindowsNativeConsumerTelemetry telemetry; std::int64_t last_timestamp{-1};
};
WindowsHardwareEncoderConsumer::WindowsHardwareEncoderConsumer(WindowsEncoderConsumerConfig c,WindowsD3D12LeaseProvider p,SubmitCallback s):impl_(std::make_unique<Impl>()){impl_->config=std::move(c);impl_->lease_provider=std::move(p);impl_->submit=std::move(s);} 
WindowsHardwareEncoderConsumer::~WindowsHardwareEncoderConsumer()=default;
DigitorResult WindowsHardwareEncoderConsumer::consume(const ProcessedGpuFramePtr& frame) noexcept {
  if(!frame||frame->backend()!=DIGITOR_RENDERER_D3D12||!frame->ready())return DIGITOR_RESULT_INVALID_ARGUMENT;
  if(!impl_->lease_provider||!impl_->submit)return DIGITOR_RESULT_NOT_INITIALIZED;
  if(impl_->config.require_zero_copy&&frame->metadata().format!=DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT)return DIGITOR_RESULT_UNSUPPORTED;
  WindowsD3D12FrameLease lease;auto r=impl_->lease_provider(frame,WindowsNativeConsumerKind::hardware_encoder,lease);
  if(r!=DIGITOR_RESULT_OK||!lease){std::scoped_lock l(impl_->mutex);++impl_->telemetry.lease_failures;return r==DIGITOR_RESULT_OK?DIGITOR_RESULT_INTERNAL_ERROR:r;}
  if(lease.timestamp_us!=frame->metadata().timestamp){std::scoped_lock l(impl_->mutex);++impl_->telemetry.timestamp_mismatches;return DIGITOR_RESULT_INTERNAL_ERROR;}
  if(lease.frame_identity!=frame->identity()){std::scoped_lock l(impl_->mutex);++impl_->telemetry.identity_mismatches;return DIGITOR_RESULT_INTERNAL_ERROR;}
  {std::scoped_lock l(impl_->mutex);if(impl_->last_timestamp>=0&&lease.timestamp_us<impl_->last_timestamp)return DIGITOR_RESULT_INVALID_ARGUMENT;}
  r=impl_->submit(lease);if(r!=DIGITOR_RESULT_OK)return r;
  std::scoped_lock l(impl_->mutex);impl_->last_timestamp=lease.timestamp_us;++impl_->telemetry.encoder_frames;impl_->telemetry.diagnostic="encoder consumed shared GPU frame";return DIGITOR_RESULT_OK;
}
WindowsZeroCopyFrameConsumer WindowsHardwareEncoderConsumer::callback(){return [this](const ProcessedGpuFramePtr& f){return consume(f);};}
DigitorResult WindowsHardwareEncoderConsumer::flush() noexcept {return DIGITOR_RESULT_OK;}
WindowsNativeConsumerTelemetry WindowsHardwareEncoderConsumer::telemetry() const {std::scoped_lock l(impl_->mutex);return impl_->telemetry;}

struct WindowsZeroCopyNativePipeline::Impl {
  std::unique_ptr<WindowsD3D12PreviewConsumer> preview;
  std::unique_ptr<WindowsHardwareEncoderConsumer> encoder;
  std::unique_ptr<WindowsZeroCopyRuntime> runtime;
};
WindowsZeroCopyNativePipeline::WindowsZeroCopyNativePipeline(WindowsZeroCopyRuntimeConfig c,WindowsZeroCopyNativeBinding b):impl_(std::make_unique<Impl>()){
  impl_->preview=std::make_unique<WindowsD3D12PreviewConsumer>(b.preview,b.lease_provider);
  impl_->encoder=std::make_unique<WindowsHardwareEncoderConsumer>(b.encoder,b.lease_provider,b.encoder_submit);
  impl_->runtime=std::make_unique<WindowsZeroCopyRuntime>(std::move(c),std::move(b.decode),impl_->preview->callback(),impl_->encoder->callback());
}
WindowsZeroCopyNativePipeline::~WindowsZeroCopyNativePipeline()=default;
DigitorResult WindowsZeroCopyNativePipeline::initialize() noexcept{return impl_->runtime->initialize();}
DigitorResult WindowsZeroCopyNativePipeline::preview(std::int64_t t) noexcept{return impl_->runtime->render_preview(t);}
DigitorResult WindowsZeroCopyNativePipeline::export_frame(std::int64_t t) noexcept{return impl_->runtime->render_export(t);}
DigitorResult WindowsZeroCopyNativePipeline::flush_export() noexcept{return impl_->encoder->flush();}
WindowsZeroCopyRuntimeTelemetry WindowsZeroCopyNativePipeline::runtime_telemetry() const{return impl_->runtime->telemetry();}
WindowsNativeConsumerTelemetry WindowsZeroCopyNativePipeline::preview_telemetry() const{return impl_->preview->telemetry();}
WindowsNativeConsumerTelemetry WindowsZeroCopyNativePipeline::encoder_telemetry() const{return impl_->encoder->telemetry();}

} // namespace digitor
