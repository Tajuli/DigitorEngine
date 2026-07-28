#pragma once
#include "digitor/media.hpp"
#include "digitor/render_graph.hpp"
#include "digitor/rgb_curves.hpp"
#include "digitor/primary_wheels.hpp"
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

// The only owner/executor of the render graph. Preview and export deliberately
// consume this class rather than maintaining separate rendering pipelines.
class SharedRenderer { public: explicit SharedRenderer(RenderGraphBuilder={}); VideoFrame render(const RenderRequest&); ProcessedGpuFramePtr render_gpu_preview(const RenderRequest&); void set_rgb_curves(std::shared_ptr<const CompiledRgbCurves> curves){curves_=std::move(curves);} void set_primary_wheels(std::shared_ptr<const PrimaryWheelsParameters>p){primary_wheels_=std::move(p);} RenderGraph& graph(){return graph_;} std::uint64_t graph_generation()const{return generation_;} private: VideoFrame render_source(const RenderRequest&); RenderGraphBuilder builder_;RenderGraph graph_;CommandQueue queue_;std::shared_ptr<const CompiledRgbCurves> curves_;std::shared_ptr<const PrimaryWheelsParameters> primary_wheels_;std::uint64_t generation_{};};
class PreviewRenderer { public:explicit PreviewRenderer(SharedRenderer&r,std::size_t cache=8):renderer_(r),cache_(cache){} void set_transform(PreviewTransform t){transform_=t;cache_.clear();gpu_cache_.clear();} const PreviewTransform&transform()const{return transform_;} std::shared_ptr<VideoFrame> frame(FrameNumber,std::uint32_t,std::uint32_t); ProcessedGpuFramePtr gpu_frame(FrameNumber,std::uint32_t,std::uint32_t); private:SharedRenderer&renderer_;PreviewTransform transform_;FrameCache<VideoFrame>cache_;std::unordered_map<FrameNumber,ProcessedGpuFramePtr>gpu_cache_;};

enum class ExportFormat { mp4,mov,mkv,png_sequence,tiff_sequence,exr_sequence,image_sequence=png_sequence };
enum class VideoCodec { h264,h265,av1 };
enum class AudioCodec { none,aac };
struct ExportProgress { FrameNumber completed{},total{}; double fraction{}; };
using ProgressCallback=std::function<void(const ExportProgress&)>;
struct ExportSettings {
    ExportFormat format{ExportFormat::mp4}; VideoCodec video_codec{VideoCodec::h264}; AudioCodec audio_codec{AudioCodec::none};
    std::uint32_t width{1920},height{1080}; FrameNumber first{},last{}; Rational frame_rate{30,1};
    std::uint64_t video_bitrate{8'000'000},audio_bitrate{192'000}; ProgressCallback progress; std::shared_ptr<std::atomic_bool> cancel;
};
class ExportRenderer {
public:
    explicit ExportRenderer(SharedRenderer& renderer) : renderer_(renderer) {}
    void export_to(const std::string&, const ExportSettings&);
private:
    VideoFrame render_frame(FrameNumber frame, const ExportSettings& settings) {
        return renderer_.render({frame, settings.width, settings.height, {}});
    }
    SharedRenderer& renderer_;
};

struct PixelValidation { double max_absolute_error{},rms_error{},psnr{},ssim{}; std::size_t differing_pixels{}; bool passed{}; };
double calculate_psnr(const VideoFrame&,const VideoFrame&);
double calculate_ssim(const VideoFrame&,const VideoFrame&);
PixelValidation validate_pixels(const VideoFrame&,const VideoFrame&,double minimum_psnr=40.0,double minimum_ssim=0.99);
// Renders once, making the returned pixels both the preview and the exact
// pre-encode export input. This is the regression framework's core invariant.
PixelValidation validate_preview_export(SharedRenderer&,const RenderRequest&,double minimum_psnr=40.0,double minimum_ssim=0.99);
}
