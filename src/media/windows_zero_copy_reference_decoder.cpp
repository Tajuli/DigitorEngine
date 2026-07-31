#include "digitor/windows_zero_copy_reference_decoder.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>

#if defined(DIGITOR_HAS_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}
#endif

namespace digitor {
struct WindowsZeroCopyReferenceDecoder::Impl {
#if defined(DIGITOR_HAS_FFMPEG)
  AVFormatContext* format{}; AVCodecContext* codec{}; AVPacket* packet{};
  AVFrame* frame{}; SwsContext* sws{}; int stream{-1}; AVStream* video{};
  std::uint64_t next{};
#endif
  std::string diagnostic;
  ~Impl(){
#if defined(DIGITOR_HAS_FFMPEG)
    sws_freeContext(sws);av_frame_free(&frame);av_packet_free(&packet);
    avcodec_free_context(&codec);avformat_close_input(&format);
#endif
  }
};

WindowsZeroCopyReferenceDecoder::WindowsZeroCopyReferenceDecoder(const std::string& path)
    : impl_(std::make_unique<Impl>()) {
#if !defined(DIGITOR_HAS_FFMPEG)
  (void)path;throw std::runtime_error("FFmpeg reference decoder unavailable");
#else
  if(avformat_open_input(&impl_->format,path.c_str(),nullptr,nullptr)<0||
     avformat_find_stream_info(impl_->format,nullptr)<0)
    throw std::runtime_error("cannot open reference media");
  const AVCodec* decoder{};
  impl_->stream=av_find_best_stream(impl_->format,AVMEDIA_TYPE_VIDEO,-1,-1,&decoder,0);
  if(impl_->stream<0||!decoder)throw std::runtime_error("reference video stream unavailable");
  impl_->video=impl_->format->streams[impl_->stream];impl_->codec=avcodec_alloc_context3(decoder);
  if(!impl_->codec||avcodec_parameters_to_context(impl_->codec,impl_->video->codecpar)<0||
     avcodec_open2(impl_->codec,decoder,nullptr)<0)throw std::runtime_error("reference decoder initialization failed");
  impl_->packet=av_packet_alloc();impl_->frame=av_frame_alloc();
  if(!impl_->packet||!impl_->frame)throw std::bad_alloc();
#endif
}
WindowsZeroCopyReferenceDecoder::~WindowsZeroCopyReferenceDecoder()=default;
const std::string& WindowsZeroCopyReferenceDecoder::diagnostic() const noexcept{return impl_->diagnostic;}

DigitorResult WindowsZeroCopyReferenceDecoder::frame(std::uint64_t index,WindowsReferenceFrame& out) noexcept {
  out={};
#if !defined(DIGITOR_HAS_FFMPEG)
  (void)index;impl_->diagnostic="FFmpeg unavailable";return DIGITOR_RESULT_UNSUPPORTED;
#else
  try{
    if(index<impl_->next){av_seek_frame(impl_->format,impl_->stream,0,AVSEEK_FLAG_BACKWARD);avcodec_flush_buffers(impl_->codec);impl_->next=0;}
    while(impl_->next<=index){
      bool got=false;
      while(!got){
        int r=avcodec_receive_frame(impl_->codec,impl_->frame);
        if(r==0){got=true;break;}
        if(r!=AVERROR(EAGAIN)&&r!=AVERROR_EOF){impl_->diagnostic="reference receive failed";return DIGITOR_RESULT_INTERNAL_ERROR;}
        if(av_read_frame(impl_->format,impl_->packet)<0){avcodec_send_packet(impl_->codec,nullptr);continue;}
        if(impl_->packet->stream_index==impl_->stream)avcodec_send_packet(impl_->codec,impl_->packet);
        av_packet_unref(impl_->packet);
      }
      if(impl_->next++!=index){av_frame_unref(impl_->frame);continue;}
      out.width=static_cast<std::uint32_t>(impl_->frame->width);out.height=static_cast<std::uint32_t>(impl_->frame->height);
      auto pts=impl_->frame->best_effort_timestamp;out.timestamp_us=pts==AV_NOPTS_VALUE?0:av_rescale_q(pts,impl_->video->time_base,AVRational{1,1000000});
      impl_->sws=sws_getCachedContext(impl_->sws,impl_->frame->width,impl_->frame->height,static_cast<AVPixelFormat>(impl_->frame->format),impl_->frame->width,impl_->frame->height,AV_PIX_FMT_RGBA64LE,SWS_BILINEAR|SWS_ACCURATE_RND,nullptr,nullptr,nullptr);
      if(!impl_->sws)return DIGITOR_RESULT_OUT_OF_MEMORY;
      std::vector<std::uint16_t> rgba(static_cast<size_t>(out.width)*out.height*4);std::uint8_t* dst[]{reinterpret_cast<std::uint8_t*>(rgba.data())};int stride[]{static_cast<int>(out.width*8)};
      if(sws_scale(impl_->sws,impl_->frame->data,impl_->frame->linesize,0,impl_->frame->height,dst,stride)!=impl_->frame->height)return DIGITOR_RESULT_INTERNAL_ERROR;
      out.linear_rgba.resize(rgba.size());constexpr float inv=1.0f/65535.0f;std::transform(rgba.begin(),rgba.end(),out.linear_rgba.begin(),[](std::uint16_t v){return v*inv;});
      av_frame_unref(impl_->frame);impl_->diagnostic.clear();return DIGITOR_RESULT_OK;
    }
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }catch(const std::bad_alloc&){return DIGITOR_RESULT_OUT_OF_MEMORY;}catch(...){return DIGITOR_RESULT_INTERNAL_ERROR;}
#endif
}
} // namespace digitor
