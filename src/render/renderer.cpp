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
  graph.rgb_curves_enabled=static_cast<bool>(curves_);
  if(primary_wheels_)graph.primary_wheels_serialization=primary_wheels_->serialize();
  if(curves_)graph.rgb_curves_serialization=curves_->serialize();
  graph.precision=preview_source_.precision;
  if(!media_sources_.empty())graph.color_metadata_identity=media_sources_.front().color_metadata_identity;
  return build_color_render_plan(purpose,media_sources_,preview_source_,graph);
}

VideoFrame SharedRenderer::render_source(const RenderRequest&r) {
  if(!r.width||!r.height||r.frame<0)throw std::invalid_argument("invalid render request");
  if (r.width > std::numeric_limits<std::size_t>::max() / r.height / sizeof(Color))
    throw std::overflow_error("render dimensions are too large");
  graph_=RenderGraph{}; VideoFrame out; out.number=r.frame; out.pts=r.frame;
  out.width=r.width; out.height=r.height;
  if(builder_)builder_(graph_,r,out);else{
    auto target=graph_.create_transient(std::uint64_t(r.width)*r.height*sizeof(Color));
    graph_.add_pass({"composite",{},{{target,ResourceState::shader_write}},[&](auto&e){
      e.dispatch([&]{out.pixels.resize(std::size_t(r.width)*r.height);});}});
  }
  graph_.compile(); graph_.execute(queue_); return out;
}

ProcessedGpuFramePtr SharedRenderer::render_gpu_preview(const RenderRequest& r) {
  if(has_media_source_policy())(void)color_render_plan(RenderPurpose::Preview);
  if (!curves_&&!primary_wheels_) throw std::logic_error("GPU preview requires a GPU color operation");
  auto source=render_source(r); ProcessedGpuFramePtr frame;
  DigitorResult result=DIGITOR_RESULT_INTERNAL_ERROR;
  if(primary_wheels_&&curves_){ProcessedGpuFramePtr intermediate;if(operation_order_==ColorOperationOrder::PrimaryWheelsThenRgbCurves){result=Engine::instance().process_primary_wheels_gpu(source.pixels,r.width,r.height,source.pts,*primary_wheels_,intermediate);if(result==DIGITOR_RESULT_OK)result=Engine::instance().process_curves_gpu(intermediate,source.pts,*curves_,frame);}else{result=Engine::instance().process_curves_gpu(source.pixels,r.width,r.height,source.pts,*curves_,intermediate);if(result==DIGITOR_RESULT_OK)result=Engine::instance().process_primary_wheels_gpu(intermediate,source.pts,*primary_wheels_,frame);}}
  else if(primary_wheels_){RenderGraph native_graph;auto input=native_graph.import_resource({.type=GraphResourceType::texture,.size=source.pixels.size()*sizeof(Color),.width=r.width,.height=r.height,.format=DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,.transient=false,.initial_state=ResourceState::shader_read,.name="Primary Wheels source"});auto output=native_graph.create_resource({.type=GraphResourceType::texture,.size=source.pixels.size()*sizeof(Color),.width=r.width,.height=r.height,.format=DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,.transient=true,.name="Primary Wheels output"});add_primary_wheels_pass(native_graph,input,output,[&](CommandEncoder&e){e.dispatch([&]{result=Engine::instance().process_primary_wheels_gpu(source.pixels,r.width,r.height,source.pts,*primary_wheels_,frame);});});native_graph.export_resource(output);native_graph.compile();native_graph.execute(queue_);graph_=std::move(native_graph);}else result=Engine::instance().process_curves_gpu(source.pixels,r.width,r.height,source.pts,*curves_,frame);
  if(result!=DIGITOR_RESULT_OK||!frame)
    throw std::runtime_error("native GPU color-operation dispatch failed");
  if(Engine::instance().present_gpu_frame(frame)!=DIGITOR_RESULT_OK)
    throw std::runtime_error("native GPU color-operation preview handoff failed");
  ++generation_; return frame;
}

VideoFrame SharedRenderer::render(const RenderRequest&r){
  auto out=render_source(r);
  if(Engine::instance().is_initialized()&&Engine::instance().renderer_info().is_gpu){
    if(curves_||primary_wheels_)
      throw std::logic_error("live GPU color preview must use render_gpu_preview");
    std::vector<uint8_t> source;source.reserve(out.pixels.size()*4);
    for(const auto& pixel:out.pixels){const auto channel=[](float value){return static_cast<uint8_t>(std::clamp(value,0.0f,1.0f)*255.0f+0.5f);};source.push_back(channel(pixel.r));source.push_back(channel(pixel.g));source.push_back(channel(pixel.b));source.push_back(channel(pixel.a));}
    std::vector<uint8_t> rgba;if(Engine::instance().render_preview_rgba8(r.width,r.height,source,rgba)!=DIGITOR_RESULT_OK)throw std::runtime_error("native preview readback failed");
    out.pixels.resize(std::size_t(r.width)*r.height);for(std::size_t n=0;n<out.pixels.size();++n)out.pixels[n]={rgba[n*4]/255.f,rgba[n*4+1]/255.f,rgba[n*4+2]/255.f,rgba[n*4+3]/255.f};
  }
  ++generation_;return out;
}
std::shared_ptr<VideoFrame> PreviewRenderer::frame(FrameNumber n,std::uint32_t w,std::uint32_t h){if(renderer_.has_media_source_policy())(void)renderer_.color_render_plan(RenderPurpose::Preview);if(auto f=cache_.get(n);f&&f->width==w&&f->height==h)return f;auto f=std::make_shared<VideoFrame>(renderer_.render({n,w,h,transform_}));cache_.put(n,f);return f;}
ProcessedGpuFramePtr PreviewRenderer::gpu_frame(FrameNumber n,std::uint32_t w,std::uint32_t h){if(auto i=gpu_cache_.find(n);i!=gpu_cache_.end()&&i->second->metadata().width==w&&i->second->metadata().height==h)return i->second;auto f=renderer_.render_gpu_preview({n,w,h,transform_});gpu_cache_[n]=f;return f;}
}
