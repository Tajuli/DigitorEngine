#include "digitor/renderer.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#ifdef DIGITOR_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif
namespace digitor {
#ifdef DIGITOR_HAS_FFMPEG
namespace {
struct Format{AVFormatContext*p{};~Format(){if(p){if(!(p->oformat->flags&AVFMT_NOFILE)&&p->pb)avio_closep(&p->pb);avformat_free_context(p);}}}; struct Codec{AVCodecContext*p{};~Codec(){avcodec_free_context(&p);}}; struct Frame{AVFrame*p{av_frame_alloc()};~Frame(){av_frame_free(&p);}}; struct Packet{AVPacket*p{av_packet_alloc()};~Packet(){av_packet_free(&p);}};
void check(int r,const char*w){if(r<0){char e[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(r,e,sizeof(e));throw std::runtime_error(std::string(w)+": "+e);}}
AVCodecID codec_id(VideoCodec c){return c==VideoCodec::h264?AV_CODEC_ID_H264:c==VideoCodec::h265?AV_CODEC_ID_HEVC:AV_CODEC_ID_AV1;} bool sequence(ExportFormat f){return f==ExportFormat::png_sequence||f==ExportFormat::tiff_sequence||f==ExportFormat::exr_sequence;} const char*extension(ExportFormat f){return f==ExportFormat::png_sequence?"png":f==ExportFormat::tiff_sequence?"tiff":"exr";} AVCodecID image_codec(ExportFormat f){return f==ExportFormat::png_sequence?AV_CODEC_ID_PNG:f==ExportFormat::tiff_sequence?AV_CODEC_ID_TIFF:AV_CODEC_ID_EXR;}
std::vector<uint8_t> rgba8(const VideoFrame&s){std::vector<uint8_t>v(s.pixels.size()*4);for(size_t i=0;i<s.pixels.size();++i){auto cv=[](float x){return uint8_t(std::clamp(x,0.f,1.f)*255+.5f);};v[i*4]=cv(s.pixels[i].r);v[i*4+1]=cv(s.pixels[i].g);v[i*4+2]=cv(s.pixels[i].b);v[i*4+3]=cv(s.pixels[i].a);}return v;}
FILE* open_binary_file(const std::filesystem::path&path){
#ifdef _WIN32
 FILE*out=nullptr;return _wfopen_s(&out,path.c_str(),L"wb")==0?out:nullptr;
#else
 return std::fopen(path.string().c_str(),"wb");
#endif
}
void send(AVCodecContext*c,AVFrame*f,AVFormatContext*o,AVStream*s){Packet packet;check(avcodec_send_frame(c,f),"send frame");for(;;){int r=avcodec_receive_packet(c,packet.p);if(r==AVERROR(EAGAIN)||r==AVERROR_EOF)break;check(r,"receive packet");av_packet_rescale_ts(packet.p,c->time_base,s->time_base);packet.p->stream_index=s->index;check(av_interleaved_write_frame(o,packet.p),"mux packet");av_packet_unref(packet.p);}}
void image(const std::filesystem::path&path,ExportFormat format,const VideoFrame&source){const AVCodec*e=avcodec_find_encoder(image_codec(format));if(!e)throw std::runtime_error("requested image encoder unavailable");Codec c;c.p=avcodec_alloc_context3(e);c.p->width=source.width;c.p->height=source.height;c.p->pix_fmt=format==ExportFormat::exr_sequence?AV_PIX_FMT_GBRPF32LE:AV_PIX_FMT_RGBA;c.p->time_base={1,1};check(avcodec_open2(c.p,e,nullptr),"open image encoder");Frame f;f.p->format=c.p->pix_fmt;f.p->width=c.p->width;f.p->height=c.p->height;check(av_frame_get_buffer(f.p,32),"allocate image");auto bytes=rgba8(source);SwsContext*x=sws_getContext(source.width,source.height,AV_PIX_FMT_RGBA,source.width,source.height,c.p->pix_fmt,SWS_BILINEAR,nullptr,nullptr,nullptr);if(!x)throw std::runtime_error("image conversion unavailable");const uint8_t*d[]={bytes.data()};int stride[]={int(source.width*4)};sws_scale(x,d,stride,0,source.height,f.p->data,f.p->linesize);sws_freeContext(x);check(avcodec_send_frame(c.p,f.p),"send image");Packet p;check(avcodec_receive_packet(c.p,p.p),"encode image");FILE*out=open_binary_file(path);if(!out)throw std::runtime_error("cannot create image");const auto written=fwrite(p.p->data,1,p.p->size,out);const int close_result=fclose(out);if(written!=static_cast<std::size_t>(p.p->size)||close_result!=0)throw std::runtime_error("cannot write image");}
}
#endif
void ExportRenderer::export_to(const std::string&path,const ExportSettings&s){
 if(path.empty()||!s.width||!s.height||s.first<0||s.last<s.first||s.frame_rate.numerator<=0||s.frame_rate.denominator<=0)throw std::invalid_argument("invalid export settings");
 if (s.last - s.first == std::numeric_limits<FrameNumber>::max())
   throw std::overflow_error("export frame range is too large");
 if (s.width > std::numeric_limits<std::size_t>::max() / s.height / 4u)
   throw std::overflow_error("export dimensions are too large");
#ifndef DIGITOR_HAS_FFMPEG
 (void)s;throw std::runtime_error("real export requires FFmpeg");
#else
 const FrameNumber total=s.last-s.first+1;if(sequence(s.format)){std::filesystem::create_directories(path);for(FrameNumber n=s.first;n<=s.last;++n){if(s.cancel&&s.cancel->load())return;auto f=render_frame(n,s);char name[64];std::snprintf(name,sizeof(name),"%08lld.%s",static_cast<long long>(n),extension(s.format));image(std::filesystem::path(path)/name,s.format,f);if(s.progress)s.progress({n-s.first+1,total,double(n-s.first+1)/total});}return;}
 const char*fmt=s.format==ExportFormat::mp4?"mp4":s.format==ExportFormat::mov?"mov":"matroska";Format out;check(avformat_alloc_output_context2(&out.p,nullptr,fmt,path.c_str()),"create muxer");const AVCodec*e=avcodec_find_encoder(codec_id(s.video_codec));if(!e)throw std::runtime_error("requested video encoder unavailable");Codec c;c.p=avcodec_alloc_context3(e);AVStream*stream=avformat_new_stream(out.p,nullptr);if(!c.p||!stream)throw std::bad_alloc();c.p->width=s.width;c.p->height=s.height;c.p->pix_fmt=AV_PIX_FMT_YUV420P;c.p->time_base={s.frame_rate.denominator,s.frame_rate.numerator};c.p->framerate={s.frame_rate.numerator,s.frame_rate.denominator};c.p->bit_rate=s.video_bitrate;c.p->gop_size=12;if(out.p->oformat->flags&AVFMT_GLOBALHEADER)c.p->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;check(avcodec_open2(c.p,e,nullptr),"open encoder");check(avcodec_parameters_from_context(stream->codecpar,c.p),"codec parameters");stream->time_base=c.p->time_base;if(!(out.p->oformat->flags&AVFMT_NOFILE))check(avio_open(&out.p->pb,path.c_str(),AVIO_FLAG_WRITE),"open output");check(avformat_write_header(out.p,nullptr),"write header");Frame f;f.p->format=c.p->pix_fmt;f.p->width=s.width;f.p->height=s.height;check(av_frame_get_buffer(f.p,32),"allocate frame");SwsContext*x=sws_getContext(s.width,s.height,AV_PIX_FMT_RGBA,s.width,s.height,c.p->pix_fmt,SWS_BILINEAR,nullptr,nullptr,nullptr);if(!x)throw std::runtime_error("conversion unavailable");try{for(FrameNumber n=s.first;n<=s.last;++n){if(s.cancel&&s.cancel->load())break;auto source=render_frame(n,s);auto bytes=rgba8(source);check(av_frame_make_writable(f.p),"writable frame");const uint8_t*d[]={bytes.data()};int stride[]={int(s.width*4)};sws_scale(x,d,stride,0,s.height,f.p->data,f.p->linesize);f.p->pts=n-s.first;send(c.p,f.p,out.p,stream);if(s.progress)s.progress({n-s.first+1,total,double(n-s.first+1)/total});}send(c.p,nullptr,out.p,stream);check(av_write_trailer(out.p),"write trailer");}catch(...){sws_freeContext(x);throw;}sws_freeContext(x);
#endif
}
}