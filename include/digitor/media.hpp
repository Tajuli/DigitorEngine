#pragma once
#include "digitor/color.hpp"
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace digitor {
using FrameNumber = std::int64_t;
struct Rational { std::int32_t numerator{1}, denominator{30}; };
// Media timestamps and durations use microseconds. Video is always converted to
// full-resolution, top-down, non-premultiplied RGBA32F in `pixels`.
enum class PixelFormat { rgba32f, rgba8, bgra8, nv12, yuv420p };
enum class ColorRange { unspecified, limited, full };
struct ColorMetadata { std::int32_t primaries{}, transfer{}, matrix{}; ColorRange range{ColorRange::unspecified}; };
struct VideoFrame { FrameNumber number{}; std::int64_t pts{}, duration{}; std::uint32_t width{}, height{}; PixelFormat pixel_format{PixelFormat::rgba32f}; ColorMetadata color; std::vector<Color> pixels; };
// Audio is interleaved native-endian float PCM in the decoder's reported layout.
struct AudioFrame { FrameNumber number{}; std::int64_t pts{}, duration{}; std::uint32_t sample_rate{48000}, channels{2}; std::uint64_t channel_layout{}; std::vector<float> samples; };
enum class HardwareDecode { automatic, cpu, dxva, videotoolbox, mediacodec };
struct DecoderOptions { HardwareDecode hardware{HardwareDecode::automatic}; bool allow_cpu_fallback{true}; std::size_t cache_capacity{16}; };
struct DecoderInfo { HardwareDecode selected{HardwareDecode::cpu}; bool hardware_accelerated{}; std::string implementation; };

template<class T> class FrameCache {
public:
    explicit FrameCache(std::size_t capacity=16):capacity_(capacity){}
    void put(FrameNumber key, std::shared_ptr<T> value) {
        erase(key); if (!capacity_) return; entries_.push_front({key,std::move(value)}); index_[key]=entries_.begin();
        if(entries_.size()>capacity_){index_.erase(entries_.back().first);entries_.pop_back();}
    }
    std::shared_ptr<T> get(FrameNumber key) {
        auto i=index_.find(key); if(i==index_.end()) return {}; entries_.splice(entries_.begin(),entries_,i->second); return i->second->second;
    }
    void erase(FrameNumber key){auto i=index_.find(key);if(i!=index_.end()){entries_.erase(i->second);index_.erase(i);}}
    void clear(){entries_.clear();index_.clear();} std::size_t size()const{return entries_.size();}
private:
    using Entry=std::pair<FrameNumber,std::shared_ptr<T>>; std::size_t capacity_; std::list<Entry> entries_;
    std::unordered_map<FrameNumber,typename std::list<Entry>::iterator> index_;
};

class VideoDecoder { public:
    virtual ~VideoDecoder()=default;
    virtual std::shared_ptr<VideoFrame> decode(FrameNumber)=0;
    virtual void seek(std::int64_t pts_us)=0;
    virtual DecoderInfo info()const=0;
    // Authoritative full-resolution sample access used by qualifier eyedroppers.
    // Implementations must sample the decoded original frame, never a proxy.
    virtual Color sample_pixel(FrameNumber frame,std::uint32_t x,std::uint32_t y) {
        auto decoded=decode(frame);
        if(!decoded)throw std::out_of_range("decoded frame unavailable");
        if(x>=decoded->width||y>=decoded->height)throw std::out_of_range("sample coordinate out of range");
        const auto index=static_cast<std::size_t>(y)*decoded->width+x;
        if(index>=decoded->pixels.size())throw std::runtime_error("decoded frame has no RGBA32F sample");
        return decoded->pixels[index];
    }
};
class AudioDecoder { public: virtual ~AudioDecoder()=default; virtual std::shared_ptr<AudioFrame> decode(FrameNumber)=0; virtual void seek(std::int64_t pts_us)=0; virtual DecoderInfo info()const=0; };
std::unique_ptr<VideoDecoder> open_video_decoder(const std::string& path, DecoderOptions options={});
std::unique_ptr<AudioDecoder> open_audio_decoder(const std::string& path, DecoderOptions options={});
bool ffmpeg_available() noexcept;
}
