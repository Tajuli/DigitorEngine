#include "digitor/renderer.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include "core/engine.hpp"
namespace digitor {
SharedRenderer::SharedRenderer(RenderGraphBuilder b):builder_(std::move(b)){}
VideoFrame SharedRenderer::render(const RenderRequest&r){if(!r.width||!r.height||r.frame<0)throw std::invalid_argument("invalid render request");graph_=RenderGraph{};VideoFrame out;out.number=r.frame;out.pts=r.frame;out.width=r.width;out.height=r.height;if(builder_)builder_(graph_,r,out);else{auto target=graph_.create_transient(std::uint64_t(r.width)*r.height*sizeof(Color));graph_.add_pass({"composite",{},{{target,ResourceState::shader_write}},[&](auto&e){e.dispatch([&]{out.pixels.resize(std::size_t(r.width)*r.height);});}});}graph_.compile();graph_.execute(queue_);if(Engine::instance().is_initialized()&&Engine::instance().renderer_info().is_gpu){std::vector<uint8_t> source;source.reserve(out.pixels.size()*4);for(const auto& pixel:out.pixels){const auto channel=[](float value){return static_cast<uint8_t>(std::clamp(value,0.0f,1.0f)*255.0f+0.5f);};source.push_back(channel(pixel.r));source.push_back(channel(pixel.g));source.push_back(channel(pixel.b));source.push_back(channel(pixel.a));}std::vector<uint8_t> rgba;if(Engine::instance().render_preview_rgba8(r.width,r.height,source,rgba)==DIGITOR_RESULT_OK){out.pixels.resize(std::size_t(r.width)*r.height);for(std::size_t n=0;n<out.pixels.size();++n)out.pixels[n]={rgba[n*4]/255.f,rgba[n*4+1]/255.f,rgba[n*4+2]/255.f,rgba[n*4+3]/255.f};}}++generation_;return out;}
std::shared_ptr<VideoFrame> PreviewRenderer::frame(FrameNumber n,std::uint32_t w,std::uint32_t h){if(auto f=cache_.get(n);f&&f->width==w&&f->height==h)return f;auto f=std::make_shared<VideoFrame>(renderer_.render({n,w,h,transform_}));cache_.put(n,f);return f;}
}
