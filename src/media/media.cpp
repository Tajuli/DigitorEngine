#include "digitor/media.hpp"
#include <algorithm>
#include <array>
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
#include <libavutil/pixdesc.h>
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

HardwareDecode public_kind(AVHWDeviceType type){
 switch(type){
  case AV_HWDEVICE_TYPE_D3D11VA:
  case AV_HWDEVICE_TYPE_DXVA2:return HardwareDecode::dxva;
  case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:return HardwareDecode::videotoolbox;
  case AV_HWDEVICE_TYPE_MEDIACODEC:return HardwareDecode::mediacodec;
  default:return HardwareDecode::cpu;
 }
}
const char* public_name(HardwareDecode kind){
 switch(kind){case HardwareDecode::dxva:return "DXVA/D3D11VA";case HardwareDecode::videotoolbox:return "VideoToolbox";case HardwareDecode::mediacodec:return "MediaCodec";case HardwareDecode::automatic:return "automatic";default:return "CPU";}
}
std::vector<AVHWDeviceType> candidates(HardwareDecode requested){
 if(requested==HardwareDecode::cpu)return {};
 if(requested==HardwareDecode::dxva)return {AV_HWDEVICE_TYPE_D3D11VA,AV_HWDEVICE_TYPE_DXVA2};
 if(requested==HardwareDecode::videotoolbox)return {AV_HWDEVICE_TYPE_VIDEOTOOLBOX};
 if(requested==HardwareDecode::mediacodec)return {AV_HWDEVICE_TYPE_MEDIACODEC};
#if defined(_WIN32)
 return {AV_HWDEVICE_TYPE_D3D11VA,AV_HWDEVICE_TYPE_DXVA2};
#elif defined(__APPLE__)
 return {AV_HWDEVICE_TYPE_VIDEOTOOLBOX};
#elif defined(__ANDROID__)
 return {AV_HWDEVICE_TYPE_MEDIACODEC};
#else
 return {};
#endif
}
int source_depth(AVPixelFormat format){
 const AVPixFmtDescriptor* d=av_pix_fmt_desc_get(format);if(!d)return 8;int depth=8;for(int i=0;i<d->nb_components;++i)depth=std::max(depth,int(d->comp[i].depth));return depth;
}

class DecoderBase {
protected:
 AVFormatContext* format_{}; AVCodecContext* codec_{}; AVPacket* packet_{}; AVFrame* frame_{};
 int stream_index_=-1; AVStream* stream_{}; FrameNumber next_number_{};
 bool input_eof_{},flush_sent_{},decoder_finished_{},packet_pending_{};
 std::int64_t seek_target_us_{};
 void cleanup(){av_frame_free(&frame_);av_packet_free(&packet_);avcodec_free_context(&codec_);avformat_close_input(&format_);}
 explicit DecoderBase(const std::string& path,AVMediaType type) try {
  int r=avformat_open_input(&format_,path.c_str(),nullptr,nullptr);if(r<0)fail("cannot open media",r);
  if((r=avformat_find_stream_info(format_,nullptr))<0)fail("cannot discover streams",r);
  const AVCodec* decoder=nullptr;stream_index_=av_find_best_stream(format_,type,-1,-1,&decoder,0);
  if(stream_index_<0)fail(type==AVMEDIA_TYPE_VIDEO?"no decodable video stream":"no decodable audio stream",stream_index_);
  stream_=format_->streams[stream_index_];codec_=avcodec_alloc_context3(decoder);if(!codec_)throw std::bad_alloc();
  if((r=avcodec_parameters_to_context(codec_,stream_->codecpar))<0)fail("cannot copy codec parameters",r);
  packet_=av_packet_alloc();frame_=av_frame_alloc();if(!packet_||!frame_)throw std::bad_alloc();
 } catch(...) { cleanup(); throw; }
 virtual ~DecoderBase(){cleanup();}
 void open_codec(const AVCodec* decoder){int r=avcodec_open2(codec_,decoder,nullptr);if(r<0)fail("unsupported codec",r);}
 bool receive(){
  if(decoder_finished_)return false;
  for(;;){
   av_frame_unref(frame_);int r=avcodec_receive_frame(codec_,frame_);if(r==0)return true;
   if(r==AVERROR_EOF){decoder_finished_=true;return false;}if(r!=AVERROR(EAGAIN))fail("decode frame",r);
   if(!input_eof_){if(!packet_pending_)r=av_read_frame(format_,packet_);else r=0;
    if(r>=0){if(packet_->stream_index!=stream_index_){av_packet_unref(packet_);continue;}r=avcodec_send_packet(codec_,packet_);if(r==AVERROR(EAGAIN)){packet_pending_=true;continue;}packet_pending_=false;av_packet_unref(packet_);if(r<0)fail("send packet",r);continue;}
    av_packet_unref(packet_);if(r!=AVERROR_EOF)fail("read packet",r);input_eof_=true;}
   if(!flush_sent_){r=avcodec_send_packet(codec_,nullptr);if(r==AVERROR_EOF){flush_sent_=true;decoder_finished_=true;return false;}if(r<0)fail("flush decoder",r);flush_sent_=true;continue;}
   decoder_finished_=true;return false;
  }
 }
 void seek_to(std::int64_t pts){const auto target=av_rescale_q(pts,engine_time_base,stream_->time_base);int r=av_seek_frame(format_,stream_index_,target,AVSEEK_FLAG_BACKWARD);if(r<0)fail("seek",r);avcodec_flush_buffers(codec_);av_packet_unref(packet_);av_frame_unref(frame_);input_eof_=flush_sent_=decoder_finished_=packet_pending_=false;next_number_=0;seek_target_us_=pts;}
 std::int64_t timestamp()const {auto p=frame_->best_effort_timestamp;return p==AV_NOPTS_VALUE?0:av_rescale_q(p,stream_->time_base,engine_time_base);}
 std::int64_t duration()const {auto d=frame_->duration;return d>0?av_rescale_q(d,stream_->time_base,engine_time_base):0;}
 bool is_preroll(std::int64_t pts)const{return seek_target_us_>0&&pts<seek_target_us_;}
};

class Video final:public VideoDecoder,private DecoderBase {
 FrameCache<VideoFrame> cache_; SwsContext* sws_{}; AVBufferRef* hw_device_{}; AVPixelFormat hw_format_{AV_PIX_FMT_NONE};
 DecoderOptions options_; DecoderInfo info_{};
 static AVPixelFormat choose_format(AVCodecContext* context,const AVPixelFormat* formats){
  auto* self=static_cast<Video*>(context->opaque);for(const AVPixelFormat* p=formats;*p!=AV_PIX_FMT_NONE;++p)if(*p==self->hw_format_)return *p;return AV_PIX_FMT_NONE;
 }
 bool configure_hardware(const AVCodec* decoder){
  for(AVHWDeviceType type:candidates(options_.hardware)){
   AVPixelFormat format=AV_PIX_FMT_NONE;
   for(int i=0;;++i){const AVCodecHWConfig* c=avcodec_get_hw_config(decoder,i);if(!c)break;if((c->methods&AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)&&c->device_type==type){format=c->pix_fmt;break;}}
   if(format==AV_PIX_FMT_NONE)continue;
   AVBufferRef* device=nullptr;const int r=av_hwdevice_ctx_create(&device,type,nullptr,nullptr,0);if(r<0)continue;
   hw_device_=device;hw_format_=format;codec_->hw_device_ctx=av_buffer_ref(hw_device_);codec_->opaque=this;codec_->get_format=&Video::choose_format;
   info_.selected=public_kind(type);info_.hardware_accelerated=true;info_.implementation=std::string("FFmpeg ")+public_name(info_.selected);info_.device_type=av_hwdevice_get_type_name(type);info_.hardware_pixel_format=av_get_pix_fmt_name(format)?av_get_pix_fmt_name(format):"unknown";info_.strict_no_silent_fallback=options_.strict_hardware_path;return true;
  }
  return false;
 }
 AVFrame* software_frame(AVFrame& transferred){
  if(!info_.hardware_accelerated)return frame_;
  if(frame_->format!=hw_format_){if(options_.strict_hardware_path)throw std::runtime_error("selected hardware decoder silently returned a software frame");return frame_;}
  if(av_hwframe_transfer_data(&transferred,frame_,0)<0)throw std::runtime_error("hardware frame transfer failed");
  av_frame_copy_props(&transferred,frame_);return &transferred;
 }
 std::shared_ptr<VideoFrame> next(){
  while(receive()&&is_preroll(timestamp())){}if(decoder_finished_)return {};
  AVFrame* transferred=av_frame_alloc();if(!transferred)throw std::bad_alloc();
  struct Guard{AVFrame*&f;~Guard(){av_frame_free(&f);}} guard{transferred};AVFrame* src=software_frame(*transferred);
  auto out=std::make_shared<VideoFrame>();out->number=next_number_++;out->pts=timestamp();out->duration=duration();out->width=src->width;out->height=src->height;out->hardware_decoded=info_.hardware_accelerated;out->source_bit_depth=static_cast<std::uint8_t>(std::clamp(source_depth(static_cast<AVPixelFormat>(src->format)),1,16));
  if(src->width<=0||src->height<=0||static_cast<std::uint64_t>(src->width)*src->height>std::numeric_limits<std::size_t>::max()/sizeof(Color))throw std::runtime_error("invalid decoded video dimensions");
  out->color.primaries=src->color_primaries;out->color.transfer=src->color_trc;out->color.matrix=src->colorspace;out->color.range=src->color_range==AVCOL_RANGE_JPEG?ColorRange::full:(src->color_range==AVCOL_RANGE_MPEG?ColorRange::limited:ColorRange::unspecified);
  // RGBA64 preserves 10/12-bit decoder output before normalization to RGBA32F.
  sws_=sws_getCachedContext(sws_,src->width,src->height,static_cast<AVPixelFormat>(src->format),src->width,src->height,AV_PIX_FMT_RGBA64LE,SWS_BICUBIC|SWS_ACCURATE_RND,nullptr,nullptr,nullptr);if(!sws_)throw std::bad_alloc();
  std::vector<std::uint16_t> rgba(static_cast<std::size_t>(src->width)*src->height*4);std::uint8_t* dst[]={reinterpret_cast<std::uint8_t*>(rgba.data())};int stride[]={src->width*8};if(sws_scale(sws_,src->data,src->linesize,0,src->height,dst,stride)!=src->height)throw std::runtime_error("pixel conversion failed");
  out->pixels.resize(static_cast<std::size_t>(src->width)*src->height);constexpr float scale=1.0f/65535.0f;for(std::size_t i=0;i<out->pixels.size();++i)out->pixels[i]={rgba[i*4]*scale,rgba[i*4+1]*scale,rgba[i*4+2]*scale,rgba[i*4+3]*scale};cache_.put(out->number,out);return out;
 }
public:
 Video(const std::string&p,DecoderOptions o):DecoderBase(p,AVMEDIA_TYPE_VIDEO),cache_(o.cache_capacity),options_(o){
  const AVCodec* decoder=avcodec_find_decoder(codec_->codec_id);if(!decoder)throw std::runtime_error("video decoder unavailable");
  const bool hardware=configure_hardware(decoder);
  if(!hardware){
   if(o.hardware!=HardwareDecode::automatic&&o.hardware!=HardwareDecode::cpu&&!o.allow_cpu_fallback)throw std::runtime_error(std::string("requested hardware decoder unavailable: ")+public_name(o.hardware));
   if(o.hardware==HardwareDecode::automatic&&!o.allow_cpu_fallback)throw std::runtime_error("no platform hardware decoder available and CPU fallback is disabled");
   info_={HardwareDecode::cpu,false,"FFmpeg software","cpu",av_get_pix_fmt_name(codec_->pix_fmt)?av_get_pix_fmt_name(codec_->pix_fmt):"software",false};
  }
  open_codec(decoder);
 }
 ~Video()override{sws_freeContext(sws_);av_buffer_unref(&hw_device_);}
 std::shared_ptr<VideoFrame> decode(FrameNumber n)override{if(n<0)throw std::out_of_range("negative frame");if(auto hit=cache_.get(n))return hit;if(n<next_number_)throw std::out_of_range("frame is no longer cached; seek before decoding it again");std::shared_ptr<VideoFrame> result;while(next_number_<=n){result=next();if(!result)return {};}return result;}
 void seek(std::int64_t p)override{if(p<0)throw std::out_of_range("negative timestamp");seek_to(p);cache_.clear();}
 DecoderInfo info()const override{return info_;}
};

class Audio final:public AudioDecoder,private DecoderBase {
 FrameCache<AudioFrame> cache_; SwrContext* swr_{};
 std::shared_ptr<AudioFrame> next(){if(!receive())return {};auto out=std::make_shared<AudioFrame>();out->number=next_number_++;out->pts=timestamp();out->sample_rate=codec_->sample_rate;out->channels=codec_->ch_layout.nb_channels;out->channel_layout=codec_->ch_layout.u.mask;int r=swr_alloc_set_opts2(&swr_,&codec_->ch_layout,AV_SAMPLE_FMT_FLT,codec_->sample_rate,&frame_->ch_layout,static_cast<AVSampleFormat>(frame_->format),frame_->sample_rate,0,nullptr);if(r<0)fail("configure resampler",r);if((r=swr_init(swr_))<0)fail("initialize resampler",r);const int capacity=av_rescale_rnd(swr_get_delay(swr_,frame_->sample_rate)+frame_->nb_samples,codec_->sample_rate,frame_->sample_rate,AV_ROUND_UP);out->samples.resize(static_cast<std::size_t>(capacity)*out->channels);std::uint8_t* destination=reinterpret_cast<std::uint8_t*>(out->samples.data());r=swr_convert(swr_,&destination,capacity,const_cast<const std::uint8_t**>(frame_->extended_data),frame_->nb_samples);if(r<0)fail("resample audio",r);out->samples.resize(static_cast<std::size_t>(r)*out->channels);out->duration=av_rescale_q(r,AVRational{1,static_cast<int>(out->sample_rate)},engine_time_base);cache_.put(out->number,out);return out;}
public:
 Audio(const std::string&p,DecoderOptions o):DecoderBase(p,AVMEDIA_TYPE_AUDIO),cache_(o.cache_capacity){const AVCodec* decoder=avcodec_find_decoder(codec_->codec_id);if(!decoder)throw std::runtime_error("audio decoder unavailable");open_codec(decoder);}
 ~Audio()override{swr_free(&swr_);}
 std::shared_ptr<AudioFrame> decode(FrameNumber n)override{if(n<0)throw std::out_of_range("negative frame");if(auto hit=cache_.get(n))return hit;if(n<next_number_)throw std::out_of_range("frame is no longer cached; seek before decoding it again");std::shared_ptr<AudioFrame> result;while(next_number_<=n){result=next();if(!result)return {};}return result;}
 void seek(std::int64_t p)override{if(p<0)throw std::out_of_range("negative timestamp");seek_to(p);cache_.clear();}
 DecoderInfo info()const override{return {HardwareDecode::cpu,false,"FFmpeg software audio","cpu","float",false};}
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
}
