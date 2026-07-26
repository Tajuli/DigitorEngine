#include "digitor/media.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

#ifdef DIGITOR_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

namespace digitor {
namespace {
#ifdef DIGITOR_HAS_FFMPEG
DecoderInfo software_info(){return {HardwareDecode::cpu,false,"FFmpeg software"};}
std::string error_text(int value){char text[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(value,text,sizeof(text));return text;}
[[noreturn]] void fail(const char* operation,int value){throw std::runtime_error(std::string(operation)+" failed with FFmpeg error "+std::to_string(value)+" ("+error_text(value)+")");}
constexpr AVRational engine_time_base{1,1000000};

class DecoderBase {
protected:
 AVFormatContext* format_{}; AVCodecContext* codec_{}; AVPacket* packet_{}; AVFrame* frame_{};
 int stream_index_=-1; AVStream* stream_{}; FrameNumber next_number_{};
 bool input_eof_{},flush_sent_{},decoder_finished_{};
 void cleanup(){av_frame_free(&frame_);av_packet_free(&packet_);avcodec_free_context(&codec_);avformat_close_input(&format_);}
 explicit DecoderBase(const std::string& path,AVMediaType type) try {
  int r=avformat_open_input(&format_,path.c_str(),nullptr,nullptr);if(r<0)fail("cannot open media",r);
  if((r=avformat_find_stream_info(format_,nullptr))<0)fail("cannot discover streams",r);
  if(!format_->nb_streams)throw std::runtime_error("cannot discover streams: container reported zero streams");
  const AVCodec* decoder=nullptr;stream_index_=av_find_best_stream(format_,type,-1,-1,&decoder,0);
  if(stream_index_<0)fail(type==AVMEDIA_TYPE_VIDEO?"no decodable video stream":"no decodable audio stream",stream_index_);
  stream_=format_->streams[stream_index_];codec_=avcodec_alloc_context3(decoder);if(!codec_)throw std::bad_alloc();
  if((r=avcodec_parameters_to_context(codec_,stream_->codecpar))<0)fail("cannot copy codec parameters",r);
  if((r=avcodec_open2(codec_,decoder,nullptr))<0)fail("unsupported codec",r);
  packet_=av_packet_alloc();frame_=av_frame_alloc();if(!packet_||!frame_)throw std::bad_alloc();
 } catch(...) { cleanup(); throw; }
 virtual ~DecoderBase(){cleanup();}
 bool receive(){
  if(decoder_finished_)return false;
  for(;;){
   av_frame_unref(frame_);
   int r=avcodec_receive_frame(codec_,frame_);
   if(r==0)return true;
   if(r==AVERROR_EOF){decoder_finished_=true;return false;}
   if(r!=AVERROR(EAGAIN))fail("decode frame",r);
   if(!input_eof_){
    r=av_read_frame(format_,packet_);
    if(r>=0){
     if(packet_->stream_index!=stream_index_){av_packet_unref(packet_);continue;}
     r=avcodec_send_packet(codec_,packet_);av_packet_unref(packet_);
     if(r<0)fail("send packet",r);
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
   // A successfully flushed decoder should return delayed frames and then
   // AVERROR_EOF. Treat an unexpected EAGAIN as a stable terminal state rather
   // than spinning or attempting to flush again.
   decoder_finished_=true;return false;
  }
 }
 void seek_to(std::int64_t pts){
  const auto target=av_rescale_q(pts,engine_time_base,stream_->time_base);int r=av_seek_frame(format_,stream_index_,target,AVSEEK_FLAG_BACKWARD);
  if(r<0)fail("seek",r);avcodec_flush_buffers(codec_);av_packet_unref(packet_);av_frame_unref(frame_);input_eof_=flush_sent_=decoder_finished_=false;next_number_=0;
 }
 std::int64_t timestamp()const {auto p=frame_->best_effort_timestamp;return p==AV_NOPTS_VALUE?0:av_rescale_q(p,stream_->time_base,engine_time_base);}
 std::int64_t duration()const {auto d=frame_->duration;return d>0?av_rescale_q(d,stream_->time_base,engine_time_base):0;}
};

class Video final:public VideoDecoder,private DecoderBase {
 FrameCache<VideoFrame> cache_; SwsContext* sws_{};
 std::shared_ptr<VideoFrame> next(){if(!receive())return {};auto out=std::make_shared<VideoFrame>();out->number=next_number_++;out->pts=timestamp();out->duration=duration();out->width=frame_->width;out->height=frame_->height;
  out->color.primaries=frame_->color_primaries;out->color.transfer=frame_->color_trc;out->color.matrix=frame_->colorspace;out->color.range=frame_->color_range==AVCOL_RANGE_JPEG?ColorRange::full:(frame_->color_range==AVCOL_RANGE_MPEG?ColorRange::limited:ColorRange::unspecified);
  sws_=sws_getCachedContext(sws_,frame_->width,frame_->height,static_cast<AVPixelFormat>(frame_->format),frame_->width,frame_->height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);if(!sws_)throw std::bad_alloc();
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(frame_->width)*frame_->height*4);std::uint8_t* dst[]={rgba.data()};int stride[]={frame_->width*4};if(sws_scale(sws_,frame_->data,frame_->linesize,0,frame_->height,dst,stride)!=frame_->height)throw std::runtime_error("pixel conversion failed");
  out->pixels.resize(static_cast<std::size_t>(frame_->width)*frame_->height);for(std::size_t i=0;i<out->pixels.size();++i)out->pixels[i]={rgba[i*4]/255.f,rgba[i*4+1]/255.f,rgba[i*4+2]/255.f,rgba[i*4+3]/255.f};cache_.put(out->number,out);return out;}
public:
 Video(const std::string&p,DecoderOptions o):DecoderBase(p,AVMEDIA_TYPE_VIDEO),cache_(o.cache_capacity){if(o.hardware!=HardwareDecode::automatic&&o.hardware!=HardwareDecode::cpu&&!o.allow_cpu_fallback)throw std::runtime_error("hardware decoding is not implemented");}
 ~Video()override{sws_freeContext(sws_);}
 std::shared_ptr<VideoFrame> decode(FrameNumber n)override{if(n<0)throw std::out_of_range("negative frame");if(decoder_finished_)return {};if(n!=next_number_)throw std::invalid_argument("decode index must equal the next sequential frame index; use seek() for random access");return next();}
 void seek(std::int64_t p)override{if(p<0)throw std::out_of_range("negative timestamp");seek_to(p);cache_.clear();}
 DecoderInfo info()const override{return software_info();}
};

class Audio final:public AudioDecoder,private DecoderBase {
 FrameCache<AudioFrame> cache_; SwrContext* swr_{};
 std::shared_ptr<AudioFrame> next(){if(!receive())return {};auto out=std::make_shared<AudioFrame>();out->number=next_number_++;out->pts=timestamp();out->sample_rate=codec_->sample_rate;out->channels=codec_->ch_layout.nb_channels;out->channel_layout=codec_->ch_layout.u.mask;
  int r=swr_alloc_set_opts2(&swr_,&codec_->ch_layout,AV_SAMPLE_FMT_FLT,codec_->sample_rate,&frame_->ch_layout,static_cast<AVSampleFormat>(frame_->format),frame_->sample_rate,0,nullptr);if(r<0)fail("configure resampler",r);if((r=swr_init(swr_))<0)fail("initialize resampler",r);
  const int capacity=av_rescale_rnd(swr_get_delay(swr_,frame_->sample_rate)+frame_->nb_samples,codec_->sample_rate,frame_->sample_rate,AV_ROUND_UP);out->samples.resize(static_cast<std::size_t>(capacity)*out->channels);std::uint8_t* destination=reinterpret_cast<std::uint8_t*>(out->samples.data());r=swr_convert(swr_,&destination,capacity,const_cast<const std::uint8_t**>(frame_->extended_data),frame_->nb_samples);if(r<0)fail("resample audio",r);out->samples.resize(static_cast<std::size_t>(r)*out->channels);out->duration=av_rescale_q(r,AVRational{1,static_cast<int>(out->sample_rate)},engine_time_base);cache_.put(out->number,out);return out;}
public:
 Audio(const std::string&p,DecoderOptions o):DecoderBase(p,AVMEDIA_TYPE_AUDIO),cache_(o.cache_capacity){if(o.hardware!=HardwareDecode::automatic&&o.hardware!=HardwareDecode::cpu&&!o.allow_cpu_fallback)throw std::runtime_error("hardware decoding is not implemented");}
 ~Audio()override{swr_free(&swr_);}
 std::shared_ptr<AudioFrame> decode(FrameNumber n)override{if(n<0)throw std::out_of_range("negative frame");if(decoder_finished_)return {};if(n!=next_number_)throw std::invalid_argument("decode index must equal the next sequential frame index; use seek() for random access");return next();}
 void seek(std::int64_t p)override{if(p<0)throw std::out_of_range("negative timestamp");seek_to(p);cache_.clear();}
 DecoderInfo info()const override{return software_info();}
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
