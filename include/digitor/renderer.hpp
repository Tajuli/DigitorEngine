#pragma once
#include "digitor/media.hpp"
#include "digitor/render_graph.hpp"
#include "digitor/rgb_curves.hpp"
#include "digitor/primary_wheels.hpp"
#include "digitor/log_wheels.hpp"
#include "digitor/render_policy.hpp"
#include "digitor/production_node_graph.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include "digitor/gpu_frame.hpp"

namespace digitor {
struct PreviewTransform { double zoom{1}; double pan_x{},pan_y{}; double rotation_degrees{}; double scale_x{1},scale_y{1}; };
struct RenderRequest { FrameNumber frame{}; std::uint32_t width{},height{}; PreviewTransform transform; };
using RenderGraphBuilder=std::function<void(RenderGraph&,const RenderRequest&,VideoFrame&)>;
using OriginalPixelSampler=std::function<Color(FrameNumber,std::uint32_t,std::uint32_t,const MediaSourceDescriptor&)>;
OriginalPixelSampler make_original_decoder_sampler(VideoDecoder&,MediaSourceDescriptor);

class SharedRenderer {
public:
  explicit SharedRenderer(RenderGraphBuilder={});
  VideoFrame render(const RenderRequest&);
  ProcessedGpuFramePtr render_gpu_preview(const RenderRequest&);
  void set_rgb_curves(std::shared_ptr<const CompiledRgbCurves> p){curves_=std::move(p);}
  void set_primary_wheels(std::shared_ptr<const PrimaryWheelsParameters> p){primary_wheels_=std::move(p);}
  void set_log_wheels(std::shared_ptr<const LogWheelsParameters> p){log_wheels_=std::move(p);}
  void set_color_operation_order(ColorOperationOrder o){operation_order_=o;}
  void set_production_node_graph(std::shared_ptr<const ProductionNodeGraph> graph){production_node_graph_=std::move(graph);}
  [[nodiscard]] const std::shared_ptr<const ProductionNodeGraph>& production_node_graph()const noexcept{return production_node_graph_;}
  void set_media_sources(std::vector<MediaSourceDescriptor> s){media_sources_=std::move(s);}
  void set_preview_source_configuration(PreviewSourceConfiguration c){preview_source_=std::move(c);}
  [[nodiscard]] ColorRenderPlan color_render_plan(RenderPurpose)const;
  [[nodiscard]] QualifierSampleRequest qualifier_sample_request(FrameNumber,double,double,RenderDimensions)const;
  void set_original_pixel_sampler(OriginalPixelSampler sampler){original_pixel_sampler_=std::move(sampler);}
  [[nodiscard]] Color sample_original_pixel(const QualifierSampleRequest&)const;
  [[nodiscard]] bool has_media_source_policy()const noexcept{return !media_sources_.empty();}
  RenderGraph& graph(){return graph_;}
  std::uint64_t graph_generation()const{return generation_;}
private:
  struct GpuRenderResult { ProcessedGpuFramePtr frame; std::string final_operation; };
  VideoFrame render_source(const RenderRequest&);
  GpuRenderResult render_color_gpu(const RenderRequest&, const VideoFrame&);
  GpuRenderResult render_production_node_graph_gpu(const RenderRequest&,const VideoFrame&);
  RenderGraphBuilder builder_; RenderGraph graph_; CommandQueue queue_;
  std::shared_ptr<const CompiledRgbCurves> curves_;
  std::shared_ptr<const PrimaryWheelsParameters> primary_wheels_;
  std::shared_ptr<const LogWheelsParameters> log_wheels_;
  std::shared_ptr<const ProductionNodeGraph> production_node_graph_;
  ColorOperationOrder operation_order_{ColorOperationOrder::PrimaryWheelsThenLogWheelsThenRgbCurves};
  std::vector<MediaSourceDescriptor> media_sources_; PreviewSourceConfiguration preview_source_; OriginalPixelSampler original_pixel_sampler_; std::uint64_t generation_{};
};
class PreviewRenderer {
public:
  explicit PreviewRenderer(SharedRenderer&r,std::size_t cache=8):renderer_(r),cache_(cache){}
  void set_transform(PreviewTransform t){transform_=t;clear_cache();}
  void set_quality(PreviewQuality q){if(quality_!=q){quality_=q;adaptive_.reset();clear_cache();}}
  [[nodiscard]] PreviewQuality quality()const noexcept{return quality_;}
  [[nodiscard]] PreviewQuality effective_quality()const noexcept{return quality_==PreviewQuality::Adaptive?adaptive_.effective_quality():quality_;}
  void set_dropped_frame_policy(DroppedFramePolicy policy)noexcept{dropped_frame_policy_=policy;}
  [[nodiscard]] DroppedFramePolicy dropped_frame_policy()const noexcept{return dropped_frame_policy_;}
  void report_frame_time(double frame_time_ms);
  [[nodiscard]] bool should_render_frame(FrameNumber frame,FrameNumber newest_requested)const noexcept;
  [[nodiscard]] std::uint64_t begin_request() noexcept{return ++latest_request_sequence_;}
  void cancel_before(std::uint64_t sequence) noexcept{cancelled_before_.store(sequence,std::memory_order_release);}
  [[nodiscard]] bool request_is_current(std::uint64_t sequence)const noexcept{return sequence==latest_request_sequence_.load(std::memory_order_acquire)&&sequence>=cancelled_before_.load(std::memory_order_acquire);}
  [[nodiscard]] bool accept_completion(std::uint64_t sequence)const noexcept{return request_is_current(sequence);}
  // Atomically applies the late-frame policy before doing any render work.
  std::shared_ptr<VideoFrame> playback_frame(FrameNumber requested,FrameNumber newest_requested,std::uint32_t,std::uint32_t);
  ProcessedGpuFramePtr playback_gpu_frame(FrameNumber requested,FrameNumber newest_requested,std::uint32_t,std::uint32_t);
  std::shared_ptr<VideoFrame> playback_frame_for_request(std::uint64_t sequence,FrameNumber requested,FrameNumber newest_requested,std::uint32_t,std::uint32_t);
  ProcessedGpuFramePtr playback_gpu_frame_for_request(std::uint64_t sequence,FrameNumber requested,FrameNumber newest_requested,std::uint32_t,std::uint32_t);
  const PreviewTransform&transform()const{return transform_;}
  [[nodiscard]] RenderDimensions resolved_dimensions(std::uint32_t,std::uint32_t)const;
  std::shared_ptr<VideoFrame> frame(FrameNumber,std::uint32_t,std::uint32_t);
  ProcessedGpuFramePtr gpu_frame(FrameNumber,std::uint32_t,std::uint32_t);
private:
  void clear_cache(){cache_.clear();gpu_cache_.clear();}
  SharedRenderer&renderer_;PreviewTransform transform_;PreviewQuality quality_{PreviewQuality::Full};
  DroppedFramePolicy dropped_frame_policy_{DroppedFramePolicy::DropLateFrames};
  AdaptivePreviewController adaptive_;
  FrameCache<VideoFrame>cache_;std::unordered_map<FrameNumber,ProcessedGpuFramePtr>gpu_cache_;
  std::atomic_uint64_t latest_request_sequence_{0};
  std::atomic_uint64_t cancelled_before_{0};
};

enum class ExportFormat { mp4,mov,mkv,png_sequence,tiff_sequence,exr_sequence,image_sequence=png_sequence };
enum class VideoCodec { h264,h265,av1 };
enum class AudioCodec { none,aac };
struct ExportProgress { FrameNumber completed{},total{}; double fraction{}; };
using ProgressCallback=std::function<void(const ExportProgress&)>;
struct ExportSettings { ExportFormat format{ExportFormat::mp4}; VideoCodec video_codec{VideoCodec::h264}; AudioCodec audio_codec{AudioCodec::none}; std::uint32_t width{1920},height{1080}; FrameNumber first{},last{}; Rational frame_rate{30,1}; std::uint64_t video_bitrate{8'000'000},audio_bitrate{192'000}; ProgressCallback progress; std::shared_ptr<std::atomic_bool> cancel; };
struct ExportFrameContract {
  FrameNumber frame{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::string original_source_identity;
  std::string color_graph_identity;
  std::string final_frame_identity;
  std::uint64_t provenance_hash{};
  DigitorRendererBackend backend{DIGITOR_RENDERER_CPU};
  std::uint64_t gpu_frame_identity{};
  std::uint64_t gpu_metadata_hash{};
};
[[nodiscard]] bool export_frame_matches_contract(const ExportFrameContract&,const ProcessedGpuFrame&);

enum class ExportSubmissionState : std::uint32_t { pending, submitted, completed, failed, cancelled };
class ExportCompletionFence final {
public:
  void mark_submitted() noexcept;
  void complete(DigitorResult result=DIGITOR_RESULT_OK) noexcept;
  void cancel() noexcept;
  [[nodiscard]] ExportSubmissionState state()const noexcept{return state_.load(std::memory_order_acquire);}
  [[nodiscard]] DigitorResult result()const noexcept{return result_.load(std::memory_order_acquire);}
  [[nodiscard]] bool wait()const;
private:
  mutable std::mutex mutex_; mutable std::condition_variable cv_;
  std::atomic<ExportSubmissionState> state_{ExportSubmissionState::pending};
  std::atomic<DigitorResult> result_{DIGITOR_RESULT_NOT_INITIALIZED};
};
using GpuEncoderSubmitCallback=std::function<DigitorResult(const ProcessedGpuFrame&,const ExportFrameContract&,const std::shared_ptr<ExportCompletionFence>&)>;
class GpuEncoderSubmission final {
public:
  explicit GpuEncoderSubmission(GpuEncoderSubmitCallback callback):callback_(std::move(callback)){}
  [[nodiscard]] std::shared_ptr<ExportCompletionFence> submit(const ProcessedGpuFramePtr&,const ExportFrameContract&)const;
private:
  GpuEncoderSubmitCallback callback_;
};


enum class HardwareEncoderPlatform : std::uint32_t { windows, android };
enum class WindowsEncoderInterop : std::uint32_t { d3d12_resource, dxgi_shared_handle, vulkan_external_memory };
enum class AndroidEncoderInterop : std::uint32_t { media_codec_input_surface };
struct HardwareEncoderTarget {
  HardwareEncoderPlatform platform{HardwareEncoderPlatform::windows};
  std::uintptr_t native_handle{};
  std::uint64_t generation{};
  WindowsEncoderInterop windows_interop{WindowsEncoderInterop::d3d12_resource};
  AndroidEncoderInterop android_interop{AndroidEncoderInterop::media_codec_input_surface};
};
using HardwareEncoderSubmitCallback=std::function<DigitorResult(const ProcessedGpuFrame&,const ExportFrameContract&,const HardwareEncoderTarget&,const std::shared_ptr<ExportCompletionFence>&)>;
class HardwareEncoderAdapter final {
public:
  HardwareEncoderAdapter(HardwareEncoderTarget target,HardwareEncoderSubmitCallback callback):target_(target),callback_(std::move(callback)){}
  [[nodiscard]] std::shared_ptr<ExportCompletionFence> submit(const ProcessedGpuFramePtr&,const ExportFrameContract&)const;
  [[nodiscard]] const HardwareEncoderTarget& target()const noexcept{return target_;}
private:
  [[nodiscard]] bool backend_compatible(DigitorRendererBackend)const noexcept;
  HardwareEncoderTarget target_; HardwareEncoderSubmitCallback callback_;
};

class EncoderResourceRetirement final {
public:
  void retain(ProcessedGpuFramePtr,const std::shared_ptr<ExportCompletionFence>&);
  std::size_t collect_completed();
  void cancel_all() noexcept;
  [[nodiscard]] std::size_t pending()const noexcept;
private:
  struct Entry{ProcessedGpuFramePtr frame;std::shared_ptr<ExportCompletionFence> fence;};
  mutable std::mutex mutex_;std::vector<Entry> entries_;
};

// Bounded encoder queue used to apply deterministic backpressure and flush semantics.
enum class ExportQueuePushResult : std::uint32_t { accepted, full, closed };
class ExportSubmissionQueue final {
public:
  explicit ExportSubmissionQueue(std::size_t capacity):capacity_(capacity){if(capacity_==0)throw std::invalid_argument("export queue capacity must be positive");}
  [[nodiscard]] ExportQueuePushResult try_push(ProcessedGpuFramePtr,const ExportFrameContract&,std::shared_ptr<ExportCompletionFence>);
  [[nodiscard]] bool try_pop(ProcessedGpuFramePtr&,ExportFrameContract&,std::shared_ptr<ExportCompletionFence>&);
  void close() noexcept;
  void cancel_pending() noexcept;
  [[nodiscard]] bool flush() const;
  [[nodiscard]] std::size_t size()const noexcept;
  [[nodiscard]] bool closed()const noexcept;
private:
  struct Item{ProcessedGpuFramePtr frame;ExportFrameContract contract;std::shared_ptr<ExportCompletionFence> fence;};
  const std::size_t capacity_; mutable std::mutex mutex_; mutable std::condition_variable cv_;
  std::vector<Item> items_; bool closed_{false};
};

// Platform session generations prevent submissions to stale Media Foundation/MediaCodec sessions.
enum class ExportWorkerState : std::uint32_t { stopped, running, draining, cancelled, failed };
struct ExportWorkerOptions {
  std::size_t max_in_flight{2};
  std::chrono::milliseconds encoder_timeout{5000};
  std::chrono::milliseconds idle_sleep{1};
};
using ExportWorkerSubmitCallback=std::function<DigitorResult(const ProcessedGpuFramePtr&,const ExportFrameContract&,const std::shared_ptr<ExportCompletionFence>&)>;
class ExportWorker final {
public:
  ExportWorker(ExportSubmissionQueue&,EncoderResourceRetirement&,ExportWorkerSubmitCallback,ExportWorkerOptions={});
  ~ExportWorker();
  void start();
  void request_drain();
  void cancel() noexcept;
  [[nodiscard]] bool wait_for_stop(std::chrono::milliseconds timeout);
  [[nodiscard]] ExportWorkerState state()const noexcept{return state_.load(std::memory_order_acquire);}
  [[nodiscard]] std::size_t in_flight()const noexcept{return in_flight_.load(std::memory_order_acquire);}
private:
  void run() noexcept;
  ExportSubmissionQueue& queue_; EncoderResourceRetirement& retirement_; ExportWorkerSubmitCallback submit_; ExportWorkerOptions options_;
  std::atomic<ExportWorkerState> state_{ExportWorkerState::stopped}; std::atomic<std::size_t> in_flight_{0};
  std::atomic_bool drain_requested_{false}; std::atomic_bool cancel_requested_{false}; std::thread thread_;
  mutable std::mutex stop_mutex_; mutable std::condition_variable stop_cv_;
};

class HardwareEncoderSession final {
public:
  explicit HardwareEncoderSession(HardwareEncoderTarget target):target_(target){}
  [[nodiscard]] HardwareEncoderTarget target()const noexcept{return target_;}
  [[nodiscard]] bool accepts(const HardwareEncoderTarget& candidate)const noexcept;
  void recreate(std::uintptr_t native_handle);
  void close() noexcept;
  [[nodiscard]] bool open()const noexcept{return open_;}
private:
  HardwareEncoderTarget target_; bool open_{true};
};

class ExportRenderer { public: explicit ExportRenderer(SharedRenderer& r):renderer_(r){} [[nodiscard]] ExportFrameContract frame_contract(FrameNumber,const ExportSettings&)const; [[nodiscard]] ExportFrameContract frame_contract(FrameNumber,const ExportSettings&,const ProcessedGpuFrame&)const; void export_to(const std::string&,const ExportSettings&); private: VideoFrame render_frame(FrameNumber f,const ExportSettings&s){if(renderer_.has_media_source_policy())(void)renderer_.color_render_plan(RenderPurpose::Export);return renderer_.render({f,s.width,s.height,{}});} SharedRenderer&renderer_; };
struct PixelValidation { double max_absolute_error{},rms_error{},psnr{},ssim{}; std::size_t differing_pixels{}; bool passed{}; };
double calculate_psnr(const VideoFrame&,const VideoFrame&); double calculate_ssim(const VideoFrame&,const VideoFrame&); PixelValidation validate_pixels(const VideoFrame&,const VideoFrame&,double minimum_psnr=40.0,double minimum_ssim=0.99); PixelValidation validate_preview_export(SharedRenderer&,const RenderRequest&,double minimum_psnr=40.0,double minimum_ssim=0.99);
[[nodiscard]] std::uint64_t deterministic_frame_hash(const VideoFrame&);
struct PreviewExportHashQualification { std::uint64_t preview_hash{},export_hash{}; PixelValidation pixels; bool passed{}; };
[[nodiscard]] PreviewExportHashQualification qualify_preview_export_hashes(const VideoFrame&,const VideoFrame&,double minimum_psnr=40.0,double minimum_ssim=0.99);
} // namespace digitor
