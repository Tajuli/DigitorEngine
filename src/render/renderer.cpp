#include "digitor/renderer.hpp"
#include "core/engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace digitor {
namespace {
std::uint64_t fnv1a64(std::string_view value) noexcept {
  std::uint64_t hash=1469598103934665603ull;
  for(unsigned char c:value){hash^=c;hash*=1099511628211ull;}
  return hash;
}
}


OriginalPixelSampler make_original_decoder_sampler(VideoDecoder& decoder,MediaSourceDescriptor source){
  if(source.source_class!=MediaSourceClass::Original)throw std::invalid_argument("qualifier sampler requires original media");
  return [&decoder,source=std::move(source)](FrameNumber frame,std::uint32_t x,std::uint32_t y,const MediaSourceDescriptor& requested){
    if(requested.source_identity!=source.source_identity||requested.width!=source.width||requested.height!=source.height)
      throw std::invalid_argument("decoder sampler source mismatch");
    return decoder.sample_pixel(frame,x,y);
  };
}

SharedRenderer::SharedRenderer(RenderGraphBuilder b):builder_(std::move(b)){}

ColorRenderPlan SharedRenderer::color_render_plan(RenderPurpose purpose) const {
  ColorGraphConfiguration graph;
  graph.operation_order=operation_order_;
  graph.primary_wheels_enabled=static_cast<bool>(primary_wheels_);
  graph.log_wheels_enabled=static_cast<bool>(log_wheels_);
  graph.rgb_curves_enabled=static_cast<bool>(curves_);
  if(primary_wheels_)graph.primary_wheels_serialization=primary_wheels_->serialize();
  if(log_wheels_)graph.log_wheels_serialization=log_wheels_->serialize();
  if(curves_)graph.rgb_curves_serialization=curves_->serialize();
  graph.precision=preview_source_.precision;
  if(!media_sources_.empty())graph.color_metadata_identity=media_sources_.front().color_metadata_identity;
  return build_color_render_plan(purpose,media_sources_,preview_source_,graph);
}

VideoFrame SharedRenderer::render_source(const RenderRequest&r) {
  if(!r.width||!r.height||r.frame<0)throw std::invalid_argument("invalid render request");
  if (r.width > std::numeric_limits<std::size_t>::max() / r.height / sizeof(Color))
    throw std::overflow_error("render dimensions are too large");
  graph_=RenderGraph{}; VideoFrame out; out.number=r.frame; out.pts=r.frame; out.width=r.width; out.height=r.height;
  if(builder_)builder_(graph_,r,out);else{auto target=graph_.create_transient(std::uint64_t(r.width)*r.height*sizeof(Color));graph_.add_pass({"composite",{},{{target,ResourceState::shader_write}},[&](auto&e){e.dispatch([&]{out.pixels.resize(std::size_t(r.width)*r.height);});}});}
  graph_.compile(); graph_.execute(queue_); return out;
}

SharedRenderer::GpuRenderResult SharedRenderer::render_color_gpu(const RenderRequest&r,const VideoFrame&source){
  const auto sequence = [&]{ ColorGraphConfiguration g; g.operation_order=operation_order_; g.primary_wheels_enabled=bool(primary_wheels_); g.log_wheels_enabled=bool(log_wheels_); g.rgb_curves_enabled=bool(curves_); return g.operation_sequence(); }();
  if(sequence.empty()) throw std::logic_error("GPU color render requires a color operation");
  ProcessedGpuFramePtr current;
  DigitorResult result=DIGITOR_RESULT_INTERNAL_ERROR;
  std::string final;
  for(const auto& operation:sequence){
    ProcessedGpuFramePtr next;
    if(operation=="primary-wheels-v1"){
      result=current?Engine::instance().process_primary_wheels_gpu(current,source.pts,*primary_wheels_,next):Engine::instance().process_primary_wheels_gpu(source.pixels,r.width,r.height,source.pts,*primary_wheels_,next);
    }else if(operation=="log-wheels-v1"){
      result=current?Engine::instance().process_log_wheels_gpu(current,source.pts,*log_wheels_,next):Engine::instance().process_log_wheels_gpu(source.pixels,r.width,r.height,source.pts,*log_wheels_,next);
    }else{
      result=current?Engine::instance().process_curves_gpu(current,source.pts,*curves_,next):Engine::instance().process_curves_gpu(source.pixels,r.width,r.height,source.pts,*curves_,next);
    }
    if(result!=DIGITOR_RESULT_OK||!next) throw std::runtime_error("native GPU color-operation dispatch failed");
    current=std::move(next); final=operation;
  }
  return {std::move(current),std::move(final)};
}

ProcessedGpuFramePtr SharedRenderer::render_gpu_preview(const RenderRequest&r){
  if(has_media_source_policy())(void)color_render_plan(RenderPurpose::Preview);
  auto source=render_source(r); auto rendered=render_color_gpu(r,source);
  if(Engine::instance().present_gpu_frame(rendered.frame)!=DIGITOR_RESULT_OK) throw std::runtime_error("native GPU color-operation preview handoff failed");
  ++generation_; return rendered.frame;
}

VideoFrame SharedRenderer::render(const RenderRequest&r){
  auto out=render_source(r);
  const bool color = bool(curves_)||bool(primary_wheels_)||bool(log_wheels_);
  if(Engine::instance().is_initialized()&&Engine::instance().renderer_info().is_gpu&&color){
    if(has_media_source_policy())(void)color_render_plan(RenderPurpose::Export);
    auto rendered=render_color_gpu(r,out);
    out.pixels.resize(std::size_t(r.width)*r.height);
    // Export consumes the final RGBA32F GPU resource through one operation-
    // independent contract. Adding or reordering operations cannot silently
    // select the wrong readback implementation. Preview still performs none.
    const auto result = Engine::instance().validation_readback_final_frame(rendered.frame,out.pixels);
    if(result!=DIGITOR_RESULT_OK) throw std::runtime_error("native GPU export readback failed");
  }else if(Engine::instance().is_initialized()&&Engine::instance().renderer_info().is_gpu){
    std::vector<uint8_t> bytes;bytes.reserve(out.pixels.size()*4);for(const auto&p:out.pixels){const auto c=[](float v){return static_cast<uint8_t>(std::clamp(v,0.f,1.f)*255.f+.5f);};bytes.insert(bytes.end(),{c(p.r),c(p.g),c(p.b),c(p.a)});}std::vector<uint8_t> rgba;if(Engine::instance().render_preview_rgba8(r.width,r.height,bytes,rgba)!=DIGITOR_RESULT_OK)throw std::runtime_error("native preview readback failed");out.pixels.resize(std::size_t(r.width)*r.height);for(std::size_t n=0;n<out.pixels.size();++n)out.pixels[n]={rgba[n*4]/255.f,rgba[n*4+1]/255.f,rgba[n*4+2]/255.f,rgba[n*4+3]/255.f};
  }
  ++generation_;return out;
}
QualifierSampleRequest SharedRenderer::qualifier_sample_request(
    FrameNumber frame,double preview_x,double preview_y,RenderDimensions preview_dimensions)const{
  if(frame<0)throw std::invalid_argument("qualifier sample frame must be non-negative");
  const auto original=color_render_plan(RenderPurpose::Export).source;
  const auto mapped=map_preview_pixel_to_source(preview_x,preview_y,preview_dimensions,original);
  const auto clamp_index=[](double value,std::uint32_t extent){
    return static_cast<std::uint32_t>(std::clamp<long long>(
      static_cast<long long>(std::llround(value)),0,static_cast<long long>(extent)-1));
  };
  return {original.source_identity,frame,clamp_index(mapped.x,original.width),
    clamp_index(mapped.y,original.height)};
}
Color SharedRenderer::sample_original_pixel(const QualifierSampleRequest& request)const{
  if(request.frame<0||request.original_source_identity.empty())throw std::invalid_argument("invalid qualifier sample request");
  const auto plan=color_render_plan(RenderPurpose::Export);
  if(plan.source.source_identity!=request.original_source_identity)throw std::invalid_argument("qualifier sample source mismatch");
  if(request.source_x>=plan.source.width||request.source_y>=plan.source.height)throw std::out_of_range("qualifier sample coordinate out of range");
  if(!original_pixel_sampler_)throw std::logic_error("original pixel sampler is not configured");
  return original_pixel_sampler_(request.frame,request.source_x,request.source_y,plan.source);
}

RenderDimensions PreviewRenderer::resolved_dimensions(std::uint32_t w,std::uint32_t h)const{
  if(renderer_.has_media_source_policy()){
    const auto plan=renderer_.color_render_plan(RenderPurpose::Preview);
    return resolve_preview_dimensions(w,h,effective_quality(),plan.preview,&plan.source);
  }
  PreviewSourceConfiguration configuration;
  return resolve_preview_dimensions(w,h,effective_quality(),configuration,nullptr);
}
std::shared_ptr<VideoFrame> PreviewRenderer::frame(FrameNumber n,std::uint32_t w,std::uint32_t h){const auto d=resolved_dimensions(w,h);if(auto f=cache_.get(n);f&&f->width==d.width&&f->height==d.height)return f;auto f=std::make_shared<VideoFrame>(renderer_.render({n,d.width,d.height,transform_}));cache_.put(n,f);return f;}
ProcessedGpuFramePtr PreviewRenderer::gpu_frame(FrameNumber n,std::uint32_t w,std::uint32_t h){const auto d=resolved_dimensions(w,h);if(auto i=gpu_cache_.find(n);i!=gpu_cache_.end()&&i->second->metadata().width==d.width&&i->second->metadata().height==d.height)return i->second;auto f=renderer_.render_gpu_preview({n,d.width,d.height,transform_});gpu_cache_[n]=f;return f;}
void PreviewRenderer::report_frame_time(double frame_time_ms){
  if(quality_!=PreviewQuality::Adaptive)return;
  const auto before=adaptive_.effective_quality();adaptive_.observe(frame_time_ms);
  if(before!=adaptive_.effective_quality())clear_cache();
}
bool PreviewRenderer::should_render_frame(FrameNumber frame,FrameNumber newest_requested)const noexcept{
  return dropped_frame_policy_==DroppedFramePolicy::Never||frame>=newest_requested;
}
std::shared_ptr<VideoFrame> PreviewRenderer::playback_frame(FrameNumber requested,FrameNumber newest_requested,std::uint32_t w,std::uint32_t h){
  if(!should_render_frame(requested,newest_requested))return {};
  return frame(requested,w,h);
}
ProcessedGpuFramePtr PreviewRenderer::playback_gpu_frame(FrameNumber requested,FrameNumber newest_requested,std::uint32_t w,std::uint32_t h){
  if(!should_render_frame(requested,newest_requested))return {};
  return gpu_frame(requested,w,h);
}
std::shared_ptr<VideoFrame> PreviewRenderer::playback_frame_for_request(std::uint64_t sequence,FrameNumber requested,FrameNumber newest_requested,std::uint32_t w,std::uint32_t h){
  if(!request_is_current(sequence)||!should_render_frame(requested,newest_requested))return {};
  auto result=frame(requested,w,h);
  return request_is_current(sequence)?result:std::shared_ptr<VideoFrame>{};
}
ProcessedGpuFramePtr PreviewRenderer::playback_gpu_frame_for_request(std::uint64_t sequence,FrameNumber requested,FrameNumber newest_requested,std::uint32_t w,std::uint32_t h){
  if(!request_is_current(sequence)||!should_render_frame(requested,newest_requested))return {};
  auto result=gpu_frame(requested,w,h);
  return request_is_current(sequence)?result:ProcessedGpuFramePtr{};
}

ExportFrameContract ExportRenderer::frame_contract(FrameNumber frame,const ExportSettings&s)const{
  if(!s.width||!s.height||frame<0)throw std::invalid_argument("invalid export frame contract");
  ExportFrameContract contract{frame,s.width,s.height,{},{},{},0};
  if(renderer_.has_media_source_policy()){const auto plan=renderer_.color_render_plan(RenderPurpose::Export);
    if(plan.source.source_class!=MediaSourceClass::Original)throw std::logic_error("export must use original media");
    contract.original_source_identity=plan.source.source_identity;contract.color_graph_identity=plan.graph.identity();
    contract.final_frame_identity=contract.original_source_identity+":"+std::to_string(frame)+":"+std::to_string(s.width)+"x"+std::to_string(s.height)+":"+contract.color_graph_identity;
    contract.provenance_hash=fnv1a64(contract.final_frame_identity);}
  return contract;
}
ExportFrameContract ExportRenderer::frame_contract(FrameNumber frame,const ExportSettings&s,const ProcessedGpuFrame& gpu)const{
  auto contract=frame_contract(frame,s);
  const auto& metadata=gpu.metadata();
  if(metadata.width!=s.width||metadata.height!=s.height||metadata.timestamp!=frame)
    throw std::invalid_argument("GPU frame metadata does not match export request");
  contract.backend=gpu.backend();
  contract.gpu_frame_identity=gpu.identity();
  const auto metadata_identity=std::to_string(metadata.width)+"x"+std::to_string(metadata.height)+":"+
    std::to_string(static_cast<unsigned>(metadata.format))+":"+
    std::to_string(static_cast<unsigned>(metadata.alpha))+":"+
    std::to_string(metadata.timestamp)+":"+metadata.color_metadata;
  contract.gpu_metadata_hash=fnv1a64(metadata_identity);
  contract.provenance_hash=fnv1a64(contract.final_frame_identity+":"+
    std::to_string(static_cast<unsigned>(contract.backend))+":"+
    std::to_string(contract.gpu_frame_identity)+":"+std::to_string(contract.gpu_metadata_hash));
  return contract;
}

bool export_frame_matches_contract(const ExportFrameContract& contract,const ProcessedGpuFrame& frame){
  const auto& metadata=frame.metadata();
  if(!frame.ready()||metadata.width!=contract.width||metadata.height!=contract.height||
     metadata.timestamp!=contract.frame)return false;
  const auto metadata_identity=std::to_string(metadata.width)+"x"+std::to_string(metadata.height)+":"+
    std::to_string(static_cast<unsigned>(metadata.format))+":"+
    std::to_string(static_cast<unsigned>(metadata.alpha))+":"+
    std::to_string(metadata.timestamp)+":"+metadata.color_metadata;
  return contract.backend==frame.backend()&&contract.gpu_frame_identity==frame.identity()&&
    contract.gpu_metadata_hash==fnv1a64(metadata_identity);
}
} // namespace digitor

namespace digitor {
void ExportCompletionFence::mark_submitted() noexcept {
  auto expected=ExportSubmissionState::pending;
  state_.compare_exchange_strong(expected,ExportSubmissionState::submitted,std::memory_order_acq_rel);
}
void ExportCompletionFence::complete(DigitorResult result) noexcept {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result_.store(result,std::memory_order_release);
    state_.store(result==DIGITOR_RESULT_OK?ExportSubmissionState::completed:ExportSubmissionState::failed,std::memory_order_release);
  }
  cv_.notify_all();
}
void ExportCompletionFence::cancel() noexcept {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result_.store(DIGITOR_RESULT_NOT_INITIALIZED,std::memory_order_release);
    state_.store(ExportSubmissionState::cancelled,std::memory_order_release);
  }
  cv_.notify_all();
}
bool ExportCompletionFence::wait()const {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock,[this]{const auto s=state();return s==ExportSubmissionState::completed||s==ExportSubmissionState::failed||s==ExportSubmissionState::cancelled;});
  return state()==ExportSubmissionState::completed&&result()==DIGITOR_RESULT_OK;
}
std::shared_ptr<ExportCompletionFence> GpuEncoderSubmission::submit(const ProcessedGpuFramePtr& frame,const ExportFrameContract& contract)const {
  if(!frame||!callback_)throw std::invalid_argument("GPU encoder submission requires frame and callback");
  if(!export_frame_matches_contract(contract,*frame))throw std::invalid_argument("GPU encoder frame violates export contract");
  auto fence=std::make_shared<ExportCompletionFence>();
  fence->mark_submitted();
  DigitorResult result=DIGITOR_RESULT_INTERNAL_ERROR;
  try { result=callback_(*frame,contract,fence); }
  catch(...) { fence->complete(DIGITOR_RESULT_INTERNAL_ERROR); return fence; }
  if(result!=DIGITOR_RESULT_OK&&fence->state()==ExportSubmissionState::submitted)fence->complete(result);
  return fence;
}
} // namespace digitor

namespace digitor {
bool HardwareEncoderAdapter::backend_compatible(DigitorRendererBackend backend)const noexcept {
  if(target_.native_handle==0||target_.generation==0)return false;
  if(target_.platform==HardwareEncoderPlatform::windows){
    if(target_.windows_interop==WindowsEncoderInterop::d3d12_resource||target_.windows_interop==WindowsEncoderInterop::dxgi_shared_handle)
      return backend==DIGITOR_RENDERER_D3D12;
    return backend==DIGITOR_RENDERER_VULKAN;
  }
  return backend==DIGITOR_RENDERER_VULKAN||backend==DIGITOR_RENDERER_OPENGL_ES;
}
std::shared_ptr<ExportCompletionFence> HardwareEncoderAdapter::submit(const ProcessedGpuFramePtr& frame,const ExportFrameContract& contract)const {
  if(!frame||!callback_)throw std::invalid_argument("hardware encoder requires frame and callback");
  if(!backend_compatible(frame->backend()))throw std::invalid_argument("GPU backend is incompatible with encoder interop target");
  if(!export_frame_matches_contract(contract,*frame))throw std::invalid_argument("hardware encoder frame violates export contract");
  auto fence=std::make_shared<ExportCompletionFence>();fence->mark_submitted();
  DigitorResult result=DIGITOR_RESULT_INTERNAL_ERROR;
  try{result=callback_(*frame,contract,target_,fence);}catch(...){fence->complete(DIGITOR_RESULT_INTERNAL_ERROR);return fence;}
  if(result!=DIGITOR_RESULT_OK&&fence->state()==ExportSubmissionState::submitted)fence->complete(result);
  return fence;
}
void EncoderResourceRetirement::retain(ProcessedGpuFramePtr frame,const std::shared_ptr<ExportCompletionFence>& fence){
  if(!frame||!fence)throw std::invalid_argument("retirement requires frame and fence");
  std::lock_guard<std::mutex> lock(mutex_);entries_.push_back({std::move(frame),fence});
}
std::size_t EncoderResourceRetirement::collect_completed(){
  std::lock_guard<std::mutex> lock(mutex_);const auto before=entries_.size();
  entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[](const Entry&e){const auto s=e.fence->state();return s==ExportSubmissionState::completed||s==ExportSubmissionState::failed||s==ExportSubmissionState::cancelled;}),entries_.end());
  return before-entries_.size();
}
void EncoderResourceRetirement::cancel_all() noexcept {std::lock_guard<std::mutex> lock(mutex_);for(auto&e:entries_)e.fence->cancel();entries_.clear();}
std::size_t EncoderResourceRetirement::pending()const noexcept {std::lock_guard<std::mutex> lock(mutex_);return entries_.size();}
} // namespace digitor

namespace digitor {
ExportQueuePushResult ExportSubmissionQueue::try_push(ProcessedGpuFramePtr frame,const ExportFrameContract& contract,std::shared_ptr<ExportCompletionFence> fence){
  if(!frame||!fence||!export_frame_matches_contract(contract,*frame))throw std::invalid_argument("invalid queued export submission");
  std::lock_guard<std::mutex> lock(mutex_);if(closed_)return ExportQueuePushResult::closed;if(items_.size()>=capacity_)return ExportQueuePushResult::full;
  items_.push_back({std::move(frame),contract,std::move(fence)});cv_.notify_all();return ExportQueuePushResult::accepted;
}
bool ExportSubmissionQueue::try_pop(ProcessedGpuFramePtr& frame,ExportFrameContract& contract,std::shared_ptr<ExportCompletionFence>& fence){
  std::lock_guard<std::mutex> lock(mutex_);if(items_.empty())return false;auto item=std::move(items_.front());items_.erase(items_.begin());frame=std::move(item.frame);contract=std::move(item.contract);fence=std::move(item.fence);cv_.notify_all();return true;
}
void ExportSubmissionQueue::close() noexcept {std::lock_guard<std::mutex> lock(mutex_);closed_=true;cv_.notify_all();}
void ExportSubmissionQueue::cancel_pending() noexcept {std::lock_guard<std::mutex> lock(mutex_);for(auto&item:items_)item.fence->cancel();items_.clear();cv_.notify_all();}
bool ExportSubmissionQueue::flush() const {std::unique_lock<std::mutex> lock(mutex_);cv_.wait(lock,[this]{return items_.empty();});return true;}
std::size_t ExportSubmissionQueue::size()const noexcept {std::lock_guard<std::mutex> lock(mutex_);return items_.size();}
bool ExportSubmissionQueue::closed()const noexcept {std::lock_guard<std::mutex> lock(mutex_);return closed_;}

bool HardwareEncoderSession::accepts(const HardwareEncoderTarget& candidate)const noexcept {
  return open_&&candidate.platform==target_.platform&&candidate.native_handle==target_.native_handle&&candidate.generation==target_.generation;
}
void HardwareEncoderSession::recreate(std::uintptr_t native_handle){if(native_handle==0)throw std::invalid_argument("encoder session handle must be non-zero");target_.native_handle=native_handle;++target_.generation;open_=true;}
void HardwareEncoderSession::close() noexcept {open_=false;target_.native_handle=0;}
} // namespace digitor

namespace digitor {
ExportWorker::ExportWorker(ExportSubmissionQueue& queue,EncoderResourceRetirement& retirement,ExportWorkerSubmitCallback submit,ExportWorkerOptions options)
 :queue_(queue),retirement_(retirement),submit_(std::move(submit)),options_(options){
  if(!submit_||options_.max_in_flight==0||options_.encoder_timeout.count()<=0)throw std::invalid_argument("invalid export worker options");
}
ExportWorker::~ExportWorker(){cancel();if(thread_.joinable())thread_.join();}
void ExportWorker::start(){
  ExportWorkerState expected=ExportWorkerState::stopped;
  if(!state_.compare_exchange_strong(expected,ExportWorkerState::running))throw std::logic_error("export worker already started");
  thread_=std::thread([this]{run();});
}
void ExportWorker::request_drain(){drain_requested_.store(true,std::memory_order_release);queue_.close();}
void ExportWorker::cancel() noexcept {cancel_requested_.store(true,std::memory_order_release);queue_.cancel_pending();queue_.close();}
bool ExportWorker::wait_for_stop(std::chrono::milliseconds timeout){
  std::unique_lock<std::mutex> lock(stop_mutex_);const bool stopped=stop_cv_.wait_for(lock,timeout,[this]{auto s=state();return s==ExportWorkerState::stopped||s==ExportWorkerState::cancelled||s==ExportWorkerState::failed;});
  if(stopped&&thread_.joinable()){thread_.join();}
  return stopped;
}
void ExportWorker::run() noexcept {
  struct Active{std::shared_ptr<ExportCompletionFence> fence;std::chrono::steady_clock::time_point started;};
  std::vector<Active> active;
  try{
    for(;;){
      if(cancel_requested_.load(std::memory_order_acquire)){
        for(auto&a:active){a.fence->cancel();}
        retirement_.cancel_all();
        state_.store(ExportWorkerState::cancelled);
        break;
      }
      const auto now=std::chrono::steady_clock::now();
      for(auto&a:active)if(a.fence->state()==ExportSubmissionState::submitted&&now-a.started>options_.encoder_timeout)a.fence->complete(DIGITOR_RESULT_INTERNAL_ERROR);
      active.erase(std::remove_if(active.begin(),active.end(),[](const Active&a){auto s=a.fence->state();return s==ExportSubmissionState::completed||s==ExportSubmissionState::failed||s==ExportSubmissionState::cancelled;}),active.end());
      retirement_.collect_completed();in_flight_.store(active.size(),std::memory_order_release);
      while(active.size()<options_.max_in_flight){
        ProcessedGpuFramePtr frame;ExportFrameContract contract;std::shared_ptr<ExportCompletionFence> fence;
        if(!queue_.try_pop(frame,contract,fence))break;
        const auto result=submit_(frame,contract,fence);
        if(fence->state()==ExportSubmissionState::pending)fence->mark_submitted();
        if(result!=DIGITOR_RESULT_OK&&fence->state()==ExportSubmissionState::submitted)fence->complete(result);
        retirement_.retain(std::move(frame),fence);active.push_back({fence,std::chrono::steady_clock::now()});
      }
      in_flight_.store(active.size(),std::memory_order_release);
      if(drain_requested_.load(std::memory_order_acquire)){
        state_.store(ExportWorkerState::draining,std::memory_order_release);
        if(queue_.size()==0&&active.empty()){state_.store(ExportWorkerState::stopped);break;}
      }
      std::this_thread::sleep_for(options_.idle_sleep);
    }
  }catch(...){for(auto&a:active)a.fence->complete(DIGITOR_RESULT_INTERNAL_ERROR);retirement_.cancel_all();state_.store(ExportWorkerState::failed);}
  std::lock_guard<std::mutex> lock(stop_mutex_);stop_cv_.notify_all();
}
} // namespace digitor
