#include "digitor/media.hpp"
#include "digitor/ffmpeg_d3d11va_surface.hpp"
#if defined(__ANDROID__)
#include "digitor/android_mediacodec_decoder.hpp"
#endif
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

NativeMediaPixelFormat native_pixel_format(AVPixelFormat format) noexcept {
  switch(format){
    case AV_PIX_FMT_NV12:return NativeMediaPixelFormat::nv12;
    case AV_PIX_FMT_P010LE:return NativeMediaPixelFormat::p010;
    case AV_PIX_FMT_YUV420P:return NativeMediaPixelFormat::yuv420p;
    case AV_PIX_FMT_YUV420P10LE:return NativeMediaPixelFormat::yuv420p10;
    case AV_PIX_FMT_BGRA:return NativeMediaPixelFormat::bgra8;
    case AV_PIX_FMT_RGBA:return NativeMediaPixelFormat::rgba8;
    default:return NativeMediaPixelFormat::unknown;
  }
}
PixelFormat engine_pixel_format(NativeMediaPixelFormat format) noexcept {
  switch(format){
    case NativeMediaPixelFormat::nv12:return PixelFormat::nv12;
    case NativeMediaPixelFormat::p010:return PixelFormat::p010;
    case NativeMediaPixelFormat::yuv420p:return PixelFormat::yuv420p;
    case NativeMediaPixelFormat::yuv420p10:return PixelFormat::yuv420p10;
    case NativeMediaPixelFormat::bgra8:return PixelFormat::bgra8;
    case NativeMediaPixelFormat::rgba8:return PixelFormat::rgba8;
    default:return PixelFormat::rgba32f;
  }
}
DecoderOptions audio_decoder_options(std::size_t cache_capacity) {
  DecoderOptions options;
  options.hardware=HardwareDecode::cpu;
  options.allow_cpu_fallback=true;
  options.cache_capacity=cache_capacity;
  options.output_mode=DecodeOutputMode::cpu_rgba32f;
  options.require_zero_copy=false;
  return options;
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
 DecoderOptions options_{};

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
 explicit DecoderBase(const std::string& path,AVMediaType type,DecoderOptions options) try : options_(options) {
  if(type==AVMEDIA_TYPE_VIDEO&&options_.require_zero_copy){
    options_.output_mode=DecodeOutputMode::native_gpu_surface;
    options_.allow_cpu_fallback=false;
    if(options_.hardware==HardwareDecode::cpu)
      throw std::runtime_error("zero-copy decode requires a hardware decoder");
  }
  int r=avformat_open_input(&format_,path.c_str(),nullptr,nullptr);if(r<0)fail("cannot open media",r);
  if((r=avformat_find_stream_info(format_,nullptr))<0)fail("cannot discover streams",r);
  if(!format_->nb_streams)throw std::runtime_error("cannot discover streams: container reported zero streams");
  const AVCodec* decoder=nullptr;stream_index_=av_find_best_stream(format_,type,-1,-1,&decoder,0);
  if(stream_index_<0)fail(type==AVMEDIA_TYPE_VIDEO?"no decodable video stream":"no decodable audio stream",stream_index_);
  stream_=format_->streams[stream_index_];codec_=avcodec_alloc_context3(decoder);if(!codec_)throw std::bad_alloc();
  if((r=avcodec_parameters_to_context(codec_,stream_->codecpar))<0)fail("cannot copy codec parameters",r);

  if(type==AVMEDIA_TYPE_VIDEO&&options_.hardware!=HardwareDecode::cpu){
   const HardwareDecode requested=options_.hardware==HardwareDecode::automatic?platform_hardware_decode():options_.hardware;
   std::string diagnostic;
   const bool configured=requested!=HardwareDecode::cpu&&configure_hardware(decoder,requested,diagnostic);
   if(!configured){
    disable_hardware();
    const bool explicit_hardware=options_.hardware!=HardwareDecode::automatic;
    if(explicit_hardware||!options_.allow_cpu_fallback)
      throw std::runtime_error("hardware video decoder unavailable: "+diagnostic);
   }
  }

  r=avcodec_open2(codec_,decoder,nullptr);
  if(r<0&&hardware_accelerated_){
   const auto diagnostic=error_text(r);
   const bool may_fallback=options_.hardware==HardwareDecode::automatic&&options_.allow_cpu_fallback&&!options_.require_zero_copy;
   if(!may_fallback)fail("open selected hardware decoder",r);
   avcodec_free_context(&codec_);
   av_buffer_unref(&hw_device_);
   codec_=avcodec_alloc_context3(decoder);if(!codec_)throw std::bad_alloc();
   if((r=avcodec_parameters_to_context(codec_,stream_->codecpar))<0)fail("cannot copy codec parameters for software fallback",r);
   disable_hardware();
   if((r=avcodec_open2(codec_,decoder,nullptr))<0)
     throw std::runtime_error("hardware decoder failed ("+diagnostic+"); software fallback also failed ("+error_text(r)+")");
  } else if(r<0) fail("unsupported codec",r);

  if(type==AVMEDIA_TYPE_VIDEO&&options_.output_mode==DecodeOutputMode::native_gpu_surface&&!hardware_accelerated_)
    throw std::runtime_error("native GPU surface output requested but hardware decode is unavailable");

  packet_=av_packet_alloc();frame_=av_frame_alloc();transfer_frame_=av_frame_alloc();
  if(!packet_||!frame_||!transfer_frame_)throw std::bad_alloc();
 } catch(...) { cleanup(); throw; }
 virtual ~DecoderBase(){cleanup();}
 AVFrame* decoded_frame(){
  if(!hardware_accelerated_||frame_->format!=hw_pixel_format_)return frame_;
  if(options_.require_zero_copy||options_.output_mode==DecodeOutputMode::native_gpu_surface)
    throw std::runtime_error("zero-copy decoder refused hardware frame download");
  av_frame_unref(transfer_frame_);
  const int r=av_hwframe_transfer_data(transfer_frame_,frame_,0);
  if(r<0)fail("transfer hardware frame",r);
  if((transfer_frame_->width!=frame_->width)||(transfer_frame_->height!=frame_->height))
    throw std::runtime_error("hardware frame transfer changed decoded dimensions");
  av_frame_copy_props(transfer_frame_,frame_);
  return transfer_frame_;
 }
 NativeMediaSurfacePtr retain_native_surface(){
  if(!hardware_accelerated_||frame_->format!=hw_pixel_format_)return {};
  auto* clone=av_frame_clone(frame_);
  if(!clone)throw std::bad_alloc();
  NativeMediaSurface::Owner owner(clone,[](void* pointer){
    auto* owned=static_cast<AVFrame*>(pointer);
    av_frame_free(&owned);
  });
  NativeMediaSurfaceDescriptor descriptor;
  descriptor.struct_size=sizeof(descriptor);
  descriptor.api_version=1;
  descriptor.width=static_cast<std::uint32_t>(frame_->width);
  descriptor.height=static_cast<std::uint32_t>(frame_->height);
  descriptor.timestamp_us=timestamp();
  descriptor.color.primaries=frame_->color_primaries;
  descriptor.color.transfer=frame_->color_trc;
  descriptor.color.matrix=frame_->colorspace;
  descriptor.color.full_range=frame_->color_range==AVCOL_RANGE_JPEG?1:0;
  descriptor.color.chroma_location=static_cast<std::uint8_t>(std::max(0,static_cast<int>(frame_->chroma_location)));

  AVPixelFormat software_format=AV_PIX_FMT_NONE;
  if(frame_->hw_frames_ctx){
    const auto* frames=reinterpret_cast<const AVHWFramesContext*>(frame_->hw_frames_ctx->data);
    if(frames)software_format=frames->sw_format;
  }
  descriptor.pixel_format=native_pixel_format(software_format);

  switch(static_cast<AVPixelFormat>(frame_->format)){
    case AV_PIX_FMT_D3D11: {
#if defined(_WIN32)
      FfmpegD3D11vaExtractionResult extracted{};
      const auto result = extract_ffmpeg_d3d11va_surface(
          frame_, timestamp(), extracted);
      if (result != DIGITOR_RESULT_OK || !extracted.surface.lifetime)
        throw std::runtime_error(extracted.diagnostic.empty()
            ? "D3D11VA surface could not be normalized to a DXGI shared handle"
            : extracted.diagnostic);
      return extracted.surface.lifetime;
#else
      return {};
#endif
    }
    case AV_PIX_FMT_VIDEOTOOLBOX:
      descriptor.platform=NativeMediaPlatform::apple;
      descriptor.handle_type=NativeMediaHandleType::cv_pixel_buffer;
      descriptor.native_handle=reinterpret_cast<std::uintptr_t>(frame_->data[3]);
      break;
    default:
      return {};
  }
  if(descriptor.native_handle==0)return {};
  return std::make_shared<NativeMediaSurface>(descriptor,std::move(owner));
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
    if(r!=AVERROR_EOF)fail("read packet",r);
    input_eof_=true;
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
  const auto target=av_rescale_q(pts,engine_time_base,stream_->time_base);
  const int r=av_seek_frame(format_,stream_index_,target,AVSEEK_FLAG_BACKWARD);
  if(r<0)fail("seek",r);
  avcodec_flush_buffers(codec_);
  av_packet_unref(packet_);
  av_frame_unref(frame_);
  av_frame_unref(transfer_frame_);
  input_eof_=flush_sent_=decoder_finished_=packet_pending_=false;next_number_=0;seek_target_us_=pts;
 }
 std::int64_t timestamp()const {auto p=frame_->best_effort_timestamp;return p==AV_NOPTS_VALUE?0:av_rescale_q(p,stream_->time_base,engine_time_base);}
 std::int64_t duration()const {auto d=frame_->duration;return d>0?av_rescale_q(d,stream_->time_base,engine_time_base):0;}
 bool is_preroll(std::int64_t pts)const{return seek_target_us_>0&&pts<seek_target_us_;}
 DecoderInfo decoder_info()const {
  NativeMediaHandleType handle=NativeMediaHandleType::none;
  if(options_.output_mode==DecodeOutputMode::native_gpu_surface||options_.require_zero_copy){
    if(hw_pixel_format_==AV_PIX_FMT_D3D11)handle=NativeMediaHandleType::dxgi_shared_handle;
    else if(hw_pixel_format_==AV_PIX_FMT_VIDEOTOOLBOX)handle=NativeMediaHandleType::cv_pixel_buffer;
  }
  return {selected_decode_,hardware_accelerated_,hardware_name(selected_decode_),handle!=NativeMediaHandleType::none,handle};
 }
};

class Video final:public VideoDecoder,private DecoderBase {
 FrameCache<VideoFrame> cache_; SwsContext* sws_{};
 std::shared_ptr<VideoFrame> next(){
  while(receive()&&is_preroll(timestamp())){}
  if(decoder_finished_)return {};
  auto out=std::make_shared<VideoFrame>();out->number=next_number_++;out->pts=timestamp();out->duration=duration();
  out->width=static_cast<std::uint32_t>(frame_->width);out->height=static_cast<std::uint32_t>(frame_->height);
  if(frame_->width<=0||frame_->height<=0||static_cast<std::uint64_t>(frame_->width)*frame_->height>std::numeric_limits<std::size_t>::max()/sizeof(Color))
    throw std::runtime_error("invalid decoded video dimensions");
  out->color.primaries=frame_->color_primaries;out->color.transfer=frame_->color_trc;out->color.matrix=frame_->colorspace;
  out->color.range=frame_->color_range==AVCOL_RANGE_JPEG?ColorRange::full:(frame_->color_range==AVCOL_RANGE_MPEG?ColorRange::limited:ColorRange::unspecified);

  const bool native_requested=options_.output_mode==DecodeOutputMode::native_gpu_surface||options_.require_zero_copy;
  if(native_requested){
    out->native_surface=retain_native_surface();
    if(!out->native_surface)
      throw std::runtime_error("hardware decoder did not expose a supported native zero-copy surface");
    out->pixel_format=engine_pixel_format(out->native_surface->descriptor().pixel_format);
    cache_.put(out->number,out);
    return out;
  }

  AVFrame* decoded=decoded_frame();
  out->width=static_cast<std::uint32_t>(decoded->width);out->height=static_cast<std::uint32_t>(decoded->height);
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
  out->pixel_format=PixelFormat::rgba32f;
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
  const std::int64_t capacity64=av_rescale_rnd(swr_get_delay(swr_,frame_->sample_rate)+frame_->nb_samples,codec_->sample_rate,frame_->sample_rate,AV_ROUND_UP);if(capacity64<=0||capacity64>std::numeric_limits<int>::max())throw std::runtime_error("resampled audio capacity is out of range");const int capacity=static_cast<int>(capacity64);out->samples.resize(static_cast<std::size_t>(capacity)*out->channels);std::uint8_t* destination=reinterpret_cast<std::uint8_t*>(out->samples.data());r=swr_convert(swr_,&destination,capacity,const_cast<const std::uint8_t**>(frame_->extended_data),frame_->nb_samples);if(r<0)fail("resample audio",r);out->samples.resize(static_cast<std::size_t>(r)*out->channels);out->duration=av_rescale_q(r,AVRational{1,static_cast<int>(out->sample_rate)},engine_time_base);cache_.put(out->number,out);return out;}
public:
 Audio(const std::string&p,DecoderOptions o):DecoderBase(p,AVMEDIA_TYPE_AUDIO,audio_decoder_options(o.cache_capacity)),cache_(o.cache_capacity){}
 ~Audio()override{swr_free(&swr_);}
 std::shared_ptr<AudioFrame> decode(FrameNumber n)override{if(n<0)throw std::out_of_range("negative frame");if(auto hit=cache_.get(n))return hit;if(n<next_number_)throw std::out_of_range("frame is no longer cached; seek before decoding it again");std::shared_ptr<AudioFrame> result;while(next_number_<=n){result=next();if(!result)return {};}return result;}
 void seek(std::int64_t p)override{if(p<0)throw std::out_of_range("negative timestamp");seek_to(p);cache_.clear();}
 DecoderInfo info()const override{return {HardwareDecode::cpu,false,"FFmpeg software",false,NativeMediaHandleType::none};}
};
#endif

#if defined(__ANDROID__) && !defined(DIGITOR_HAS_FFMPEG)
PixelFormat android_native_pixel_format(NativeMediaPixelFormat value) {
  switch (value) {
    case NativeMediaPixelFormat::nv12:
      return PixelFormat::nv12;
    case NativeMediaPixelFormat::p010:
      return PixelFormat::p010;
    default:
      throw std::runtime_error(
          "Android MediaCodec returned an unsupported native pixel format");
  }
}

AndroidMediaCodecSessionConfig android_decoder_config(
    const std::string& path, const DecoderOptions& options) {
  if (path.empty())
    throw std::invalid_argument("Android media path is required");
  if (options.hardware != HardwareDecode::automatic &&
      options.hardware != HardwareDecode::mediacodec) {
    throw std::runtime_error(
        "Android native media fallback supports automatic/MediaCodec decode only");
  }
  if (options.output_mode == DecodeOutputMode::cpu_rgba32f) {
    throw std::runtime_error(
        "Android native MediaCodec fallback exposes GPU surfaces only");
  }
  AndroidMediaCodecSessionConfig config{};
  config.media_path = path;
  config.max_acquired_images =
      static_cast<std::uint32_t>(std::max<std::size_t>(2, options.cache_capacity));
  config.dequeue_timeout_us = 10'000;
  config.scheduling = AndroidDecodeScheduling::frame_accurate;
  // The auxiliary media facade may be tolerant. Production registered decode
  // remains strict and owns its own MediaCodec session.
  config.strict_zero_copy = options.require_zero_copy;
  return config;
}

class AndroidNativeVideo final : public VideoDecoder {
 public:
  AndroidNativeVideo(const std::string& path, DecoderOptions options)
      : decoder_(android_decoder_config(path, options)),
        cache_(std::max<std::size_t>(2, options.cache_capacity)) {
    const auto result = decoder_.initialize();
    if (result != DIGITOR_RESULT_OK) {
      const auto diagnostic = decoder_.diagnostic();
      throw std::runtime_error(
          diagnostic.empty()
              ? "Android native MediaCodec decoder initialization failed"
              : diagnostic);
    }
  }

  std::shared_ptr<VideoFrame> decode(FrameNumber frame_number) override {
    if (frame_number < 0) throw std::out_of_range("negative frame");
    if (auto cached = cache_.get(frame_number)) return cached;
    if (frame_number < next_number_) {
      throw std::out_of_range(
          "Android native frame is no longer cached; seek before decoding it again");
    }

    std::shared_ptr<VideoFrame> result;
    while (next_number_ <= frame_number) {
      NativeMediaSurfacePtr surface;
      const auto decode_result = decoder_.decode_next(surface);
      if (decode_result != DIGITOR_RESULT_OK) {
        if (decoder_.statistics().eos_drained) return {};
        const auto diagnostic = decoder_.diagnostic();
        throw std::runtime_error(
            diagnostic.empty() ? "Android native MediaCodec decode failed"
                               : diagnostic);
      }
      if (!surface) {
        throw std::runtime_error(
            "Android native MediaCodec returned no AHardwareBuffer surface");
      }
      const auto& descriptor = surface->descriptor();
      if (descriptor.platform != NativeMediaPlatform::android ||
          descriptor.handle_type != NativeMediaHandleType::ahardware_buffer ||
          descriptor.native_handle == 0 || descriptor.width == 0 ||
          descriptor.height == 0) {
        throw std::runtime_error(
            "Android native MediaCodec returned an invalid AHardwareBuffer descriptor");
      }

      auto frame = std::make_shared<VideoFrame>();
      frame->number = next_number_++;
      frame->pts = descriptor.timestamp_us;
      frame->duration = 0;
      frame->width = descriptor.width;
      frame->height = descriptor.height;
      frame->pixel_format = android_native_pixel_format(descriptor.pixel_format);
      frame->color.primaries = descriptor.color.primaries;
      frame->color.transfer = descriptor.color.transfer;
      frame->color.matrix = descriptor.color.matrix;
      frame->color.range = descriptor.color.full_range ? ColorRange::full
                                                       : ColorRange::limited;
      frame->native_surface = std::move(surface);
      cache_.put(frame->number, frame);
      result = std::move(frame);
    }
    return result;
  }

  void seek(std::int64_t pts_us) override {
    if (pts_us < 0) throw std::out_of_range("negative timestamp");
    const auto result = decoder_.seek(pts_us);
    if (result != DIGITOR_RESULT_OK) {
      const auto diagnostic = decoder_.diagnostic();
      throw std::runtime_error(
          diagnostic.empty() ? "Android native MediaCodec seek failed"
                             : diagnostic);
    }
    cache_.clear();
    next_number_ = 0;
  }

  DecoderInfo info() const override {
    return {HardwareDecode::mediacodec,
            true,
            "Android NDK MediaCodec/AImageReader AHardwareBuffer",
            true,
            NativeMediaHandleType::ahardware_buffer};
  }

 private:
  AndroidMediaCodecAhbDecoder decoder_;
  FrameCache<VideoFrame> cache_;
  FrameNumber next_number_{};
};
#endif
}

std::unique_ptr<VideoDecoder> open_video_decoder(const std::string&p,DecoderOptions o){
#ifdef DIGITOR_HAS_FFMPEG
 return std::make_unique<Video>(p,o);
#elif defined(__ANDROID__)
 return std::make_unique<AndroidNativeVideo>(p,o);
#else
 (void)p;(void)o;throw std::runtime_error("DigitorEngine was built without FFmpeg or a platform-native video decoder");
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
#elif defined(__ANDROID__)
 return HardwareDecode::mediacodec;
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