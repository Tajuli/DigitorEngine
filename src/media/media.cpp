#include "digitor/media.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

#ifdef DIGITOR_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

namespace digitor {
namespace {
#ifdef DIGITOR_HAS_FFMPEG
std::string error_text(int value){char text[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(value,text,sizeof(text));return text;}
[[noreturn]] void fail(const char* operation,int value){throw std::runtime_error(std::string(operation)+" failed with FFmpeg error "+std::to_string(value)+" ("+error_text(value)+")");}
constexpr AVRational engine_time_base{1,1000000};

AVHWDeviceType requested_hw_device(HardwareDecode requested) noexcept {
  switch(requested){
    case HardwareDecode::dxva: return AV_HWDEVICE_TYPE_D3D11VA;
    case HardwareDecode::videotoolbox: return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
    case HardwareDecode::mediacodec: return AV_HWDEVICE_TYPE_MEDIACODEC;
    default: return AV_HWDEVICE_TYPE_NONE;
  }
}
HardwareDecode platform_hardware_decode() noexcept {
#if defined(_WIN32)
  return HardwareDecode::dxva;
#elif defined(__APPLE__)
  return HardwareDecode::videotoolbox;
#elif defined(__ANDROID__)
  return HardwareDecode::mediacodec;
#else
  return HardwareDecode::cpu;
#endif
}
const char* hardware_name(HardwareDecode value) noexcept {
  switch(value){
    case HardwareDecode::dxva: return "FFmpeg D3D11VA";
    case HardwareDecode::videotoolbox: return "FFmpeg VideoToolbox";
    case HardwareDecode::mediacodec: return "FFmpeg MediaCodec";
    case HardwareDecode::cpu: return "FFmpeg software";
    case HardwareDecode::automatic: return "FFmpeg automatic";
  }
  return "FFmpeg unknown";
}

class DecoderBase {
protected:
 AVFormatContext* format_{}; AVCodecContext* codec_{}; AVPacket* packet_{}; AVFrame* frame_{}; AVFrame* transfer_frame_{};
 AVBufferRef* hw_device_{}; AVPixelFormat hw_pixel_format_{AV_PIX_FMT_NONE};
 int stream_index_=-1; AVStream* stream_{}; FrameNumber next_number_{};
 bool input_eof_{},flush_sent_{},decoder_finished_{},packet_pending_{};
 std::int64_t seek_target_us_{};
 HardwareDecode selected_decode_{HardwareDecode::cpu};
 bool hardware_accelerated_{};

 static AVPixelFormat choose_pixel_format(AVCodecContext* context,const AVPixelFormat* formats) {
  auto* self=static_cast<DecoderBase*>(context->opaque);
  if(!self||!formats)return AV_PIX_FMT_NONE;
  for(const AVPixelFormat* current=formats;*current!=AV_PIX_FMT_NONE;++current)
    if(*current==self->hw_pixel_format_)return *current;
  return formats[0];
 }
 bool configure_hardware(const AVCodec* decoder,HardwareDecode requested,std::string& diagnostic){
  const AVHWDeviceType type=requested_hw_device(requested);
  if(type==AV_HWDEVICE_TYPE_NONE){diagnostic="requested hardware decoder is unsupported on this build";return false;}
  const AVCodecHWConfig* selected_config=nullptr;
  for(int index=0;;++index){
   const auto* config=avcodec_get_hw_config(decoder,index);
   if(!config)break;
   if((config->methods&AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)&&config->device_type==type){selected_config=config;break;}
  }
  if(!selected_config){diagnostic=std::string("codec does not expose ")+av_hwdevice_get_type_name(type)+" hardware configuration";return false;}
  int result=av_hwdevice_ctx_create(&hw_device_,type,nullptr,nullptr,0);
  if(result<0){diagnostic=std::string("cannot create ")+av_hwdevice_get_type_name(type)+" device: "+error_text(result);return false;}
  hw_pixel_format_=selected_config->pix_fmt;
  codec_->hw_device_ctx=av_buffer_ref(hw_device_);
  if(!codec_->hw_device_ctx){diagnostic="cannot retain hardware device context";av_buffer_unref(&hw_device_);hw_pixel_format_=AV_PIX_FMT_NONE;return false;}
  codec_->opaque=this;
  codec_->get_format=&DecoderBase::choose_pixel_format;
  selected_decode_=requested;
  hardware_accelerated_=true;
  return true;
 }
 void disable_hardware() noexcept {
  if(codec_){av_buffer_unref(&codec_->hw_device_ctx);codec_->opaque=nullptr;codec_->get_format=nullptr;}
  av_buffer_unref(&hw_device_);
  hw_pixel_format_=AV_PIX_FMT_NONE;
  selected_decode_=HardwareDecode::cpu;
  hardware_accelerated_=false;
 }
 void cleanup(){av_frame_free(&transfer_frame_);av_frame_free(&frame_);av_packet_free(&packet_);avcodec_free_context(&codec_);av_buffer_unref(&hw_device_);avformat_close_input(&format_);}
 explicit DecoderBase(const std::string& path,AVMediaType type,DecoderOptions options) try {
  int r=avformat_open_input(&format_,path.c_str(),nullptr,nullptr);if(r<0)fail("cannot open media",r);
  if((r=avformat_find_stream_info(format_,nullptr))<0)fail("cannot discover streams",r);
  if(!format_->nb_streams)throw std::runtime_error("cannot discover streams: container reported zero streams");
  const AVCodec* decoder=nullptr;stream_index_=av_find_best_stream(format_,type,-1,-1,&decoder,0);
  if(stream_index_<0)fail(type==AVMEDIA_TYPE_VIDEO?"no decodable video stream":"no decodable audio stream",stream_index_);
  stream_=format_->streams[stream_index_];codec_=avcodec_alloc_context3(decoder);if(!codec_)throw std::bad_alloc();
  if((r=avcodec_parameters_to_context(codec_,stream_->codecpar))<0)fail("cannot copy codec parameters",r);

  if(type==AVMEDIA_TYPE_VIDEO&&options.hardware!=HardwareDecode::cpu){
   const HardwareDecode requested=options.hardware==HardwareDecode::automatic?platform_hardware_decode():options.hardware;
   std::string diagnostic;
   const bool configured=requested!=HardwareDecode::cpu&&configure_hardware(decoder,requested,diagnostic);
   if(!configured){
    disable_hardware();
    const bool explicit_hardware=options.hardware!=HardwareDecode::automatic;
    if(explicit_hardware||!options.allow_cpu_fallback)
      throw std::runtime_error("hardware video decoder unavailable: "+diagnostic);
   }
  }

  r=avcodec_open2(codec_,decoder,nullptr);
  if(r<0&&hardware_accelerated_){
   const auto diagnostic=error_text(r);
   const bool may_fallback=options.hardware==HardwareDecode::automatic&&options.allow_cpu_fallback;
   if(!may_fallback)fail("open selected hardware decoder",r);
   avcodec_free_context(&codec_);
   av_buffer_unref(&hw_device_);
   codec_=avcodec_alloc_context3(decoder);if(!codec_)throw std::bad_alloc();
   if((r=avcodec_parameters_to_context(codec_,stream_->codecpar))<0)fail("cannot copy codec parameters for software fallback",r);
   disable_hardware();
   if((r=avcodec_open2(codec_,decoder,nullptr))<0)
     throw std::runtime_error("hardware decoder failed ("+diagnostic+"); software fallback also failed ("+error_text(r)+")");
  } else if(r<0) fail("unsupported codec",r);

  packet_=av_packet_alloc();frame_=av_frame_alloc();transfer_frame_=av_frame_alloc();
  if(!packet_||!frame_||!transfer_frame_)throw std::bad_alloc();
 } catch(...) { cleanup(); throw; }
 virtual ~DecoderBase(){cleanup();}
 AVFrame* decoded_frame(){
  if(!hardware_accelerated_||frame_->format!=hw_pixel_format_)return frame_;
  av_frame_unref(transfer_frame_);
  const int r=av_hwframe_transfer_data(transfer_frame_,frame_,0);
  if(r<0)fail("transfer hardware frame",r);
  if((transfer_frame_->width!=frame_->width)||(transfer_frame_->height!=frame_->height))
    throw std::runtime_error("hardware frame transfer changed decoded dimensions");
  av_frame_copy_props(transfer_frame_,frame_);
  return transfer_frame_;
 }
 bool receive(){
  if(decoder_finished_)return false;
  for(;;){
   av_frame_unref(frame_);
   int r=avcodec_receive_frame(codec_,frame_);
   if(r==0)return true;
   if(r==AVERROR_EOF){decoder_finished_=true;return false;}
   if(r!=AVERROR(EAGAIN))fail(hardware_accelerated_?"decode hardware frame":"decode frame",r);
   if(!input_eof_){
    if(!packet_pending_) r=av_read_frame(format_,packet_); else r=0;
    if(r>=0){
     if(packet_->stream_index!=stream_index_){av_packet_unref(packet_);continue;}
     r=avcodec_send_packet(codec_,packet_);
     if(r==AVERROR(EAGAIN)){packet_pending_=true;continue;}
     packet_pending_=false;av_packet_unref(packet_);
     if(r<0)fail(hardware_accelerated_?"send packet to hardware decoder":"send packet",r);
     continue;
    }
    av_packet_unref(packet_);
    if(r!=AVERROR_EOF)fail("read packet",r);input_eof_=true;
   }
   if(!flush_sent_){
    r=avcodec_send_packet(codec_,nullptr);
    if(r==AVERROR_EOF){flush_sent_=true;decoder_finished_=true;return false;}
    if(r<0)fail("flush decoder",r);
    flush_sent_=true;continue;
   }
   decoder_finished_=true;return false;
  }
 }
 void seek_to(std::int64_t pts){
  const auto target=av_rescale_q(pts,engine_time_base,stream_->time_base);int r=av_seek_frame(format_,stream_index_,target,AVSEEK_FLAG_BACKWARD);
  if(r<0)fail("seek",r);avcodec_flush_buffers(codec_);av_packet_unref(packet_);av_frame_unref(frame_);av_frame_unref(transfer_frame_);
  input_eof_=flush_sent_=decoder_finished_=packet_pending_=false;next_number_=0;seek_target_us_=pts;
 }
 std::int64_t timestamp()const {auto p=frame_->best_effort_timestamp;return p==AV_NOPTS_VALUE?0:av_rescale_q(p,stream_->time_base,engine_time_base);}
 std::int64_t duration()const {auto d=frame_->duration;return d>0?av_rescale_q(d,stream_->time_base,engine_time_base):0;}
 bool is_preroll(std::int64_t pts)const{return seek_target_us_>0&&pts<seek_target_us_;}
 DecoderInfo decoder_info()const {return {selected_decode_,hardware_accelerated_,hardware_name(selected_decode_)};}
};

class Video final:public VideoDecoder,private DecoderBase {
 FrameCache<VideoFrame> cache_; SwsContext* sws_{};
 std::shared_ptr<VideoFrame> next(){
  while(receive()&&is_preroll(timestamp())){}
  if(decoder_finished_)return {};
  AVFrame* decoded=decoded_frame();
  auto out=std::make_shared<VideoFrame>();out->number=next_number_++;out->pts=timestamp();out->duration=duration();
  out->width=decoded->width;out->height=decoded->height;
  if(decoded->width<=0||decoded->height<=0||static_cast<std::uint64_t>(decoded->width)*decoded->height>std::numeric_limits<std::size_t>::max()/sizeof(Color))
    throw std::runtime_error("invalid decoded video dimensions");
  out->color.primaries=decoded->color_primaries;out->color.transfer=decoded->color_trc;out->color.matrix=decoded->colorspace;
  out->color.range=decoded->color_range==AVCOL_RANGE_JPEG?ColorRange::full:(decoded->color_range==AVCOL_RANGE_MPEG?ColorRange::limited:ColorRange::unspecified);

  constexpr int flags=SWS_BILINEAR|SWS_ACCURATE_RND|SWS_FULL_CHR_H_INT|SWS_FULL_CHR_H_INP;
  sws_=sws_getCachedContext(sws_,decoded->width,decoded->height,static_cast<AVPixelFormat>(decoded->format),
      decoded->width,decoded->height,AV_PIX_FMT_RGBA64LE,flags,nullptr,nullptr,nullptr);
  if(!sws_)throw std::bad_alloc();
  const int* coefficients=sws_getCoefficients(decoded->colorspace==AVCOL_SPC_BT2020_NCL?SWS_CS_BT2020:
      decoded->colorspace==AVCOL_SPC_BT709?SWS_CS_ITU709:SWS_CS_ITU601);
  const int source_range=decoded->color_range==AVCOL_RANGE_JPEG?1:0;
  if(sws_setColorspaceDetails(sws_,coefficients,source_range,coefficients,1,0,1<<16,1<<16)<0)
    throw std::runtime_error("cannot configure color-accurate pixel conversion");

  std::vector<std::uint16_t> rgba(static_cast<std::size_t>(decoded->width)*decoded->height*4);
  std::uint8_t* dst[]={reinterpret_cast<std::uint8_t*>(rgba.data())};int stride[]={decoded->width*8};
  if(sws_scale(sws_,decoded->data,decoded->linesize,0,decoded->height,dst,stride)!=decoded->height)
    throw std::runtime_error("pixel conversion failed");
  out->pixels.resize(static_cast<std::size_t>(decoded->width)*decoded->height);
  constexpr float inverse=1.0f/65535.0f;
  for(std::size_t i=0;i<out->pixels.size();++i)
    out->pixels[i]={rgba[i*4]*inverse,rgba[i*4+1]*inverse,rgba[i*4+2]*inverse,rgba[i*4+3]*inverse};
  cache_.put(out->number,out);return out;
 }
public:
 Video(const std::string&p,DecoderOptions o):DecoderBase(p,AVMEDIA_TYPE_VIDEO,o),cache_(o.cache_capacity){}
 ~Video()override{sws_freeContext(sws_);}
 std::shared_ptr<VideoFrame> decode(FrameNumber n)override{if(n<0)throw std::out_of_range("negative frame");if(auto hit=cache_.get(n))return hit;if(n<next_number_)throw std::out_of_range("frame is no longer cached; seek before decoding it again");std::shared_ptr<VideoFrame> result;while(next_number_<=n){result=next();if(!result)return {};}return result;}
 void seek(std::int64_t p)override{if(p<0)throw std::out_of_range("negative timestamp");seek_to(p);cache_.clear();}
 DecoderInfo info()const override{return decoder_info();}
};

class Audio final:public AudioDecoder,private DecoderBase {
 FrameCache<AudioFrame> cache_; SwrContext* swr_{};
 std::shared_ptr<AudioFrame> next(){if(!receive())return {};auto out=std::make_shared<AudioFrame>();out->number=next_number_++;out->pts=timestamp();out->sample_rate=codec_->sample_rate;out->channels=codec_->ch_layout.nb_channels;out->channel_layout=codec_->ch_layout.u.mask;
  int r=swr_alloc_set_opts2(&swr_,&codec_->ch_layout,AV_SAMPLE_FMT_FLT,codec_->sample_rate,&frame_->ch_layout,static_cast<AVSampleFormat>(frame_->format),frame_->sample_rate,0,nullptr);if(r<0)fail("configure resampler",r);if((r=swr_init(swr_))<0)fail("initialize resampler",r);
  const int capacity=av_rescale_rnd(swr_get_delay(swr_,frame_->sample_rate)+frame_->nb_samples,codec_->sample_rate,frame_->sample_rate,AV_ROUND_UP);out->samples.resize(static_cast<std::size_t>(capacity)*out->channels);std::uint8_t* destination=reinterpret_cast<std::uint8_t*>(out->samples.data());r=swr_convert(swr_,&destination,capacity,const_cast<const std::uint8_t**>(frame_->extended_data),frame_->nb_samples);if(r<0)fail("resample audio",r);out->samples.resize(static_cast<std::size_t>(r)*out->channels);out->duration=av_rescale_q(r,AVRational{1,static_cast<int>(out->sample_rate)},engine_time_base);cache_.put(out->number,out);return out;}
public:
 Audio(const std::string&p,DecoderOptions o):DecoderBase(p,AVMEDIA_TYPE_AUDIO,DecoderOptions{HardwareDecode::cpu,true,o.cache_capacity}),cache_(o.cache_capacity){}
 ~Audio()override{swr_free(&swr_);}
 std::shared_ptr<AudioFrame> decode(FrameNumber n)override{if(n<0)throw std::out_of_range("negative frame");if(auto hit=cache_.get(n))return hit;if(n<next_number_)throw std::out_of_range("frame is no longer cached; seek before decoding it again");std::shared_ptr<AudioFrame> result;while(next_number_<=n){result=next();if(!result)return {};}return result;}
 void seek(std::int64_t p)override{if(p<0)throw std::out_of_range("negative timestamp");seek_to(p);cache_.clear();}
 DecoderInfo info()const override{return {HardwareDecode::cpu,false,"FFmpeg software"};}
};
#endif
}

std::unique_ptr<VideoDecoder> open_video_decoder(const std::string&p,DecoderOptions o){
#ifdef DIGITOR_HAS_FFMPEG
 return std::make_unique<Video>(p,o);
#else
 (void)p;(void)o;throw std::runtime_error("DigitorEngine was built without FFmpeg");
#endif
}
std::unique_ptr<AudioDecoder> open_audio_decoder(const std::string&p,DecoderOptions o){
#ifdef DIGITOR_HAS_FFMPEG
 return std::make_unique<Audio>(p,o);
#else
 (void)p;(void)o;throw std::runtime_error("DigitorEngine was built without FFmpeg");
#endif
}
bool ffmpeg_available()noexcept{
#ifdef DIGITOR_HAS_FFMPEG
 return true;
#else
 return false;
#endif
}
HardwareDecode preferred_hardware_decode() noexcept {
#ifdef DIGITOR_HAS_FFMPEG
 return platform_hardware_decode();
#else
 return HardwareDecode::cpu;
#endif
}
const char* hardware_decode_name(HardwareDecode value) noexcept {
#ifdef DIGITOR_HAS_FFMPEG
 return hardware_name(value);
#else
 switch(value){
   case HardwareDecode::automatic:return "automatic";
   case HardwareDecode::cpu:return "cpu";
   case HardwareDecode::dxva:return "D3D11VA";
   case HardwareDecode::videotoolbox:return "VideoToolbox";
   case HardwareDecode::mediacodec:return "MediaCodec";
 }
 return "unknown";
#endif
}
}
