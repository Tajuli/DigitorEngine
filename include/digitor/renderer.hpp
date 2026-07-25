#pragma once
#include "digitor/media.hpp"
#include "digitor/render_graph.hpp"
#include <functional>
#include <string>
namespace digitor {
struct PreviewTransform { double zoom{1}; double pan_x{},pan_y{}; double rotation_degrees{}; double scale_x{1},scale_y{1}; };
struct RenderRequest { FrameNumber frame{}; std::uint32_t width{},height{}; PreviewTransform transform; };
using RenderGraphBuilder=std::function<void(RenderGraph&,const RenderRequest&,VideoFrame&)>;
class SharedRenderer { public: explicit SharedRenderer(RenderGraphBuilder={}); VideoFrame render(const RenderRequest&); RenderGraph& graph(){return graph_;} std::uint64_t graph_generation()const{return generation_;} private:RenderGraphBuilder builder_;RenderGraph graph_;CommandQueue queue_;std::uint64_t generation_{};};
class PreviewRenderer { public:explicit PreviewRenderer(SharedRenderer&r,std::size_t cache=8):renderer_(r),cache_(cache){} void set_transform(PreviewTransform t){transform_=t;cache_.clear();} const PreviewTransform&transform()const{return transform_;} std::shared_ptr<VideoFrame> frame(FrameNumber,std::uint32_t,std::uint32_t); private:SharedRenderer&renderer_;PreviewTransform transform_;FrameCache<VideoFrame>cache_;};
enum class ExportFormat { mp4,mov,mkv,image_sequence };
struct ExportSettings {ExportFormat format{ExportFormat::mp4};std::uint32_t width{1920},height{1080};FrameNumber first{},last{};};
class ExportRenderer {public:explicit ExportRenderer(SharedRenderer&r):renderer_(r){}void export_to(const std::string&,const ExportSettings&);private:SharedRenderer&renderer_;};
}
