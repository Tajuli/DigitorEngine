#include "digitor/windows_zero_copy_media_host.hpp"
#include "digitor/windows_d3d12_yuv_converter.hpp"
#include "digitor/windows_zero_copy_import.hpp"

#include <fstream>
#include <memory>
#include <utility>

#if defined(_WIN32) && defined(DIGITOR_HAS_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}
#endif

namespace digitor {

struct WindowsZeroCopyMediaHost::Impl {
  void* device{};
  WindowsZeroCopyMediaHostOptions options;
  WindowsZeroCopyMediaHostInfo info;
  std::unique_ptr<WindowsD3D12YuvConverter> converter;
  std::unique_ptr<WindowsD3D12ZeroCopyImporter> importer;
  std::unique_ptr<FfmpegD3D11vaZeroCopyDecoder> decoder;
#if defined(_WIN32) && defined(DIGITOR_HAS_FFMPEG)
  AVFormatContext* format{};
  AVCodecContext* codec{};
  AVBufferRef* hw_device{};
  AVPacket* packet{};
  AVFrame* frame{};
  int stream_index{-1};
  AVRational stream_time_base{1,1};
#endif
  ~Impl() {
#if defined(_WIN32) && defined(DIGITOR_HAS_FFMPEG)
    av_frame_free(&frame); av_packet_free(&packet);
    avcodec_free_context(&codec); av_buffer_unref(&hw_device);
    avformat_close_input(&format);
#endif
  }
};

WindowsZeroCopyMediaHost::WindowsZeroCopyMediaHost(
    void* device, WindowsZeroCopyMediaHostOptions options)
    : impl_(std::make_unique<Impl>()) {
  impl_->device=device; impl_->options=std::move(options);
}
WindowsZeroCopyMediaHost::~WindowsZeroCopyMediaHost()=default;

DigitorResult WindowsZeroCopyMediaHost::open(std::string* diagnostic) noexcept {
  auto fail=[&](DigitorResult r,const char* text){if(diagnostic)*diagnostic=text;return r;};
#if !defined(_WIN32) || !defined(DIGITOR_HAS_FFMPEG)
  return fail(DIGITOR_RESULT_UNSUPPORTED,"Windows FFmpeg media host unavailable");
#else
  if(!impl_->device||impl_->options.media_path.empty())
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT,"D3D12 device and media path are required");
  try {
    int r=avformat_open_input(&impl_->format,impl_->options.media_path.c_str(),nullptr,nullptr);
    if(r<0)return fail(DIGITOR_RESULT_INVALID_ARGUMENT,"cannot open qualification media");
    if(avformat_find_stream_info(impl_->format,nullptr)<0)
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT,"cannot discover qualification media streams");
    const AVCodec* dec{};
    impl_->stream_index=av_find_best_stream(impl_->format,AVMEDIA_TYPE_VIDEO,-1,-1,&dec,0);
    if(impl_->stream_index<0||!dec)return fail(DIGITOR_RESULT_UNSUPPORTED,"no decodable video stream");
    auto* stream=impl_->format->streams[impl_->stream_index]; impl_->stream_time_base=stream->time_base;
    impl_->codec=avcodec_alloc_context3(dec); if(!impl_->codec)return DIGITOR_RESULT_OUT_OF_MEMORY;
    if(avcodec_parameters_to_context(impl_->codec,stream->codecpar)<0)
      return fail(DIGITOR_RESULT_INTERNAL_ERROR,"cannot copy codec parameters");
    if(av_hwdevice_ctx_create(&impl_->hw_device,AV_HWDEVICE_TYPE_D3D11VA,nullptr,nullptr,0)<0)
      return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,"cannot create D3D11VA device");
    impl_->codec->hw_device_ctx=av_buffer_ref(impl_->hw_device);
    if(!impl_->codec->hw_device_ctx)return DIGITOR_RESULT_OUT_OF_MEMORY;
    if(avcodec_open2(impl_->codec,dec,nullptr)<0)
      return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,"cannot open D3D11VA decoder");
    impl_->packet=av_packet_alloc(); impl_->frame=av_frame_alloc();
    if(!impl_->packet||!impl_->frame)return DIGITOR_RESULT_OUT_OF_MEMORY;
    impl_->converter=std::make_unique<WindowsD3D12YuvConverter>(impl_->device);
    impl_->importer=std::make_unique<WindowsD3D12ZeroCopyImporter>(impl_->device,impl_->converter->callback());
    FfmpegD3D11vaZeroCopyOptions policy{};
    policy.fallback=impl_->options.strict_gpu_first
      ? FfmpegD3D11vaFallbackPolicy::strict_gpu_first
      : FfmpegD3D11vaFallbackPolicy::allow_explicit_legacy_fallback;
    impl_->decoder=std::make_unique<FfmpegD3D11vaZeroCopyDecoder>(*impl_->importer,policy);
    impl_->info.codec=dec->name?dec->name:"unknown";
    impl_->info.width=impl_->codec->width; impl_->info.height=impl_->codec->height;
    const auto rate=av_guess_frame_rate(impl_->format,stream,nullptr);
    impl_->info.frame_rate=rate.den?double(rate.num)/double(rate.den):0.0;
    impl_->info.hardware_decode=true;
    if(diagnostic)diagnostic->clear(); return DIGITOR_RESULT_OK;
  } catch(...) { return fail(DIGITOR_RESULT_INTERNAL_ERROR,"unexpected media-host initialization failure"); }
#endif
}

DigitorResult WindowsZeroCopyMediaHost::qualify(
    const WindowsZeroCopyThresholds& thresholds,
    WindowsZeroCopyQualificationReport& report,
    std::string* diagnostic) noexcept {
#if !defined(_WIN32) || !defined(DIGITOR_HAS_FFMPEG)
  (void)thresholds;(void)report; if(diagnostic)*diagnostic="unsupported build"; return DIGITOR_RESULT_UNSUPPORTED;
#else
  if(!impl_->decoder||!impl_->codec)return DIGITOR_RESULT_INVALID_ARGUMENT;
  auto provider=[this](std::uint32_t,void*& out,std::int64_t& timestamp)->DigitorResult {
    out=nullptr;
    for(;;){
      av_frame_unref(impl_->frame);
      int r=avcodec_receive_frame(impl_->codec,impl_->frame);
      if(r==0){
        if(impl_->frame->format!=AV_PIX_FMT_D3D11)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        const auto pts=impl_->frame->best_effort_timestamp==AV_NOPTS_VALUE?0:impl_->frame->best_effort_timestamp;
        timestamp=av_rescale_q(pts,impl_->stream_time_base,AVRational{1,1000000});
        out=impl_->frame; ++impl_->info.decoded_frames; impl_->info.d3d11va_surface=true;
        const char* name=av_get_pix_fmt_name(static_cast<AVPixelFormat>(impl_->frame->format));
        impl_->info.pixel_format=name?name:"d3d11"; return DIGITOR_RESULT_OK;
      }
      if(r!=AVERROR(EAGAIN))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      r=av_read_frame(impl_->format,impl_->packet);
      if(r<0){avcodec_send_packet(impl_->codec,nullptr);continue;}
      if(impl_->packet->stream_index!=impl_->stream_index){av_packet_unref(impl_->packet);continue;}
      r=avcodec_send_packet(impl_->codec,impl_->packet); av_packet_unref(impl_->packet);
      if(r<0&&r!=AVERROR(EAGAIN))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
  };
  // The CPU reference/readback are intentionally injected by the qualification
  // executable so production playback never gains an implicit readback path.
  WindowsZeroCopyReferenceProvider reference=[](std::uint32_t,std::vector<float>&)->DigitorResult{
    return DIGITOR_RESULT_UNSUPPORTED;
  };
  WindowsZeroCopyReadback readback=[](const ProcessedGpuFramePtr&,std::vector<float>&)->DigitorResult{
    return DIGITOR_RESULT_UNSUPPORTED;
  };
  WindowsZeroCopyQualificationRunner runner(*impl_->decoder,provider,reference,readback,{});
  const auto result=runner.run(thresholds,report);
  if(!impl_->options.report_path.empty()){
    std::ofstream file(impl_->options.report_path,std::ios::binary);
    if(file)file<<windows_zero_copy_report_json(report);
  }
  if(diagnostic)*diagnostic=report.diagnostic;
  return result;
#endif
}

const WindowsZeroCopyMediaHostInfo& WindowsZeroCopyMediaHost::info() const noexcept {
  return impl_->info;
}

} // namespace digitor
