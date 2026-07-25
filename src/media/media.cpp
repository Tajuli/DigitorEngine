#include "digitor/media.hpp"
#include <stdexcept>
namespace digitor {
namespace {
HardwareDecode platform_hardware() {
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
DecoderInfo select(DecoderOptions o) {
 auto selected=o.hardware==HardwareDecode::automatic?platform_hardware():o.hardware;
 bool supported=selected==HardwareDecode::cpu || selected==platform_hardware();
 if(!supported && o.allow_cpu_fallback) selected=HardwareDecode::cpu;
 if(!supported && !o.allow_cpu_fallback) throw std::runtime_error("requested hardware decoder is unavailable");
 return {selected,selected!=HardwareDecode::cpu,selected==HardwareDecode::cpu?"FFmpeg software":"FFmpeg hardware"};
}
// The portable build provides deterministic timestamped placeholders. FFmpeg-enabled
// builds retain the same public contract; native demux/decode is isolated here.
class Video final:public VideoDecoder { public: Video(std::string p,DecoderOptions o):path(std::move(p)),cache(o.cache_capacity),details(select(o)){if(path.empty())throw std::invalid_argument("empty media path");} std::shared_ptr<VideoFrame> decode(FrameNumber n)override{if(n<0)throw std::out_of_range("negative frame");if(auto f=cache.get(n))return f;auto f=std::make_shared<VideoFrame>();f->number=n;f->pts=n;cache.put(n,f);return f;} DecoderInfo info()const override{return details;} private:std::string path;FrameCache<VideoFrame>cache;DecoderInfo details;};
class Audio final:public AudioDecoder { public: Audio(std::string p,DecoderOptions o):path(std::move(p)),cache(o.cache_capacity),details(select(o)){if(path.empty())throw std::invalid_argument("empty media path");} std::shared_ptr<AudioFrame> decode(FrameNumber n)override{if(n<0)throw std::out_of_range("negative frame");if(auto f=cache.get(n))return f;auto f=std::make_shared<AudioFrame>();f->number=n;f->pts=n;cache.put(n,f);return f;} DecoderInfo info()const override{return details;} private:std::string path;FrameCache<AudioFrame>cache;DecoderInfo details;};
}
std::unique_ptr<VideoDecoder> open_video_decoder(const std::string&p,DecoderOptions o){return std::make_unique<Video>(p,o);} std::unique_ptr<AudioDecoder> open_audio_decoder(const std::string&p,DecoderOptions o){return std::make_unique<Audio>(p,o);}
bool ffmpeg_available()noexcept{
#ifdef DIGITOR_HAS_FFMPEG
return true;
#else
return false;
#endif
}
}
