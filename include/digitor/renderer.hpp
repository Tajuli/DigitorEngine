#pragma once
#include "digitor/media.hpp"
#include "digitor/render_graph.hpp"
#include "digitor/rgb_curves.hpp"
#include "digitor/primary_wheels.hpp"
#include "digitor/log_wheels.hpp"
#include "digitor/render_policy.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "digitor/gpu_frame.hpp"

namespace digitor {
struct PreviewTransform { double zoom{1}; double pan_x{},pan_y{}; double rotation_degrees{}; double scale_x{1},scale_y{1}; };
struct RenderRequest { FrameNumber frame{}; std::uint32_t width{},height{}; PreviewTransform transform; };
using RenderGraphBuilder=std::function<void(RenderGraph&,const RenderRequest&,VideoFrame&)>;

class SharedRenderer {
public:
  explicit SharedRenderer(RenderGraphBuilder={});
  VideoFrame render(const RenderRequest&);
  ProcessedGpuFramePtr render_gpu_preview(const RenderRequest&);
  void set_rgb_curves(std::shared_ptr<const CompiledRgbCurves> p){curves_=std::move(p);}
  void set_primary_wheels(std::shared_ptr<const PrimaryWheelsParameters> p){primary_wheels_=std::move(p);}
  void set_log_wheels(std::shared_ptr<const LogWheelsParameters> p){log_wheels_=std::move(p);}
  void set_color_operation_order(ColorOperationOrder o){operation_order_=o;}
  void set_media_sources(std::vector<MediaSourceDescriptor> s){media_sources_=std::move(s);}
  void set_preview_source_configuration(PreviewSourceConfiguration c){preview_source_=std::move(c);}
  [[nodiscard]] ColorRenderPlan color_render_plan(RenderPurpose)const;
  [[nodiscard]] bool has_media_source_policy()const noexcept{return !media_sources_.empty();}
  RenderGraph& graph(){return graph_;}
  std::uint64_t graph_generation()const{return generation_;}
private:
  struct GpuRenderResult { ProcessedGpuFramePtr frame; std::string final_operation; };
  VideoFrame render_source(const RenderRequest&);
  GpuRenderResult render_color_gpu(const RenderRequest&, const VideoFrame&);
  RenderGraphBuilder builder_; RenderGraph graph_; CommandQueue queue_;
  std::shared_ptr<const CompiledRgbCurves> curves_;
  std::shared_ptr<const PrimaryWheelsParameters> primary_wheels_;
  std::shared_ptr<const LogWheelsParameters> log_wheels_;
  ColorOperationOrder operation_order_{ColorOperationOrder::PrimaryWheelsThenLogWheelsThenRgbCurves};
  std::vector<MediaSourceDescriptor> media_sources_; PreviewSourceConfiguration preview_source_; std::uint64_t generation_{};
};
class PreviewRenderer { public:explicit PreviewRenderer(SharedRenderer&r,std::size_t cache=8):renderer_(r),cache_(cache){} void set_transform(PreviewTransform t){transform_=t;cache_.clear();gpu_cache_.clear();} const PreviewTransform&transform()const{return transform_;} std::shared_ptr<VideoFrame> frame(FrameNumber,std::uint32_t,std::uint32_t); ProcessedGpuFramePtr gpu_frame(FrameNumber,std::uint32_t,std::uint32_t); private:SharedRenderer&renderer_;PreviewTransform transform_;FrameCache<VideoFrame>cache_;std::unordered_map<FrameNumber,ProcessedGpuFramePtr>gpu_cache_;};

enum class ExportFormat { mp4,mov,mkv,png_sequence,tiff_sequence,exr_sequence,image_sequence=png_sequence };
enum class VideoCodec { h264,h265,av1 };
enum class AudioCodec { none,aac };
struct ExportProgress { FrameNumber completed{},total{}; double fraction{}; };
using ProgressCallback=std::function<void(const ExportProgress&)>;
struct ExportSettings { ExportFormat format{ExportFormat::mp4}; VideoCodec video_codec{VideoCodec::h264}; AudioCodec audio_codec{AudioCodec::none}; std::uint32_t width{1920},height{1080}; FrameNumber first{},last{}; Rational frame_rate{30,1}; std::uint64_t video_bitrate{8'000'000},audio_bitrate{192'000}; ProgressCallback progress; std::shared_ptr<std::atomic_bool> cancel; };
class ExportRenderer { public: explicit ExportRenderer(SharedRenderer& r):renderer_(r){} void export_to(const std::string&,const ExportSettings&); private: VideoFrame render_frame(FrameNumber f,const ExportSettings&s){if(renderer_.has_media_source_policy())(void)renderer_.color_render_plan(RenderPurpose::Export);return renderer_.render({f,s.width,s.height,{}});} SharedRenderer&renderer_; };
struct PixelValidation { double max_absolute_error{},rms_error{},psnr{},ssim{}; std::size_t differing_pixels{}; bool passed{}; };
double calculate_psnr(const VideoFrame&,const VideoFrame&); double calculate_ssim(const VideoFrame&,const VideoFrame&); PixelValidation validate_pixels(const VideoFrame&,const VideoFrame&,double minimum_psnr=40.0,double minimum_ssim=0.99); PixelValidation validate_preview_export(SharedRenderer&,const RenderRequest&,double minimum_psnr=40.0,double minimum_ssim=0.99);
} // namespace digitor
