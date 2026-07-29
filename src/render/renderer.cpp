#include "digitor/renderer.hpp"
#include "core/engine.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace digitor {
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
    // Validation/export readback is deliberately separated from direct preview.
    // All native owners use the same RGBA32F resource contract; select the
    // operation-specific evidence seam when Log Wheels produced the final frame.
    const auto result = rendered.final_operation=="log-wheels-v1"
      ? Engine::instance().validation_readback_log_wheels(rendered.frame,out.pixels)
      : Engine::instance().validation_readback_primary_wheels(rendered.frame,out.pixels);
    if(result!=DIGITOR_RESULT_OK) throw std::runtime_error("native GPU export readback failed");
  }else if(Engine::instance().is_initialized()&&Engine::instance().renderer_info().is_gpu){
    std::vector<uint8_t> bytes;bytes.reserve(out.pixels.size()*4);for(const auto&p:out.pixels){const auto c=[](float v){return static_cast<uint8_t>(std::clamp(v,0.f,1.f)*255.f+.5f);};bytes.insert(bytes.end(),{c(p.r),c(p.g),c(p.b),c(p.a)});}std::vector<uint8_t> rgba;if(Engine::instance().render_preview_rgba8(r.width,r.height,bytes,rgba)!=DIGITOR_RESULT_OK)throw std::runtime_error("native preview readback failed");out.pixels.resize(std::size_t(r.width)*r.height);for(std::size_t n=0;n<out.pixels.size();++n)out.pixels[n]={rgba[n*4]/255.f,rgba[n*4+1]/255.f,rgba[n*4+2]/255.f,rgba[n*4+3]/255.f};
  }
  ++generation_;return out;
}
std::shared_ptr<VideoFrame> PreviewRenderer::frame(FrameNumber n,std::uint32_t w,std::uint32_t h){if(renderer_.has_media_source_policy())(void)renderer_.color_render_plan(RenderPurpose::Preview);if(auto f=cache_.get(n);f&&f->width==w&&f->height==h)return f;auto f=std::make_shared<VideoFrame>(renderer_.render({n,w,h,transform_}));cache_.put(n,f);return f;}
ProcessedGpuFramePtr PreviewRenderer::gpu_frame(FrameNumber n,std::uint32_t w,std::uint32_t h){if(auto i=gpu_cache_.find(n);i!=gpu_cache_.end()&&i->second->metadata().width==w&&i->second->metadata().height==h)return i->second;auto f=renderer_.render_gpu_preview({n,w,h,transform_});gpu_cache_[n]=f;return f;}
} // namespace digitor
