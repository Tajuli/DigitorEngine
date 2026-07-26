#include "digitor/media.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "CHECK failed: " #condition << " at " << __FILE__ << ':' << __LINE__ << '\n'; \
    return 1; } } while (false)

namespace {
void put_u16(std::ofstream& out,unsigned value){out.put(static_cast<char>(value));out.put(static_cast<char>(value>>8));}
void put_u32(std::ofstream& out,unsigned value){put_u16(out,value);put_u16(out,value>>16);}
void write_wav(const std::filesystem::path& path){
 const short samples[]={-32768,-20000,-10000,0,10000,20000,30000,32767};
 std::ofstream out(path,std::ios::binary);out.write("RIFF",4);put_u32(out,36+sizeof(samples));out.write("WAVEfmt ",8);
 put_u32(out,16);put_u16(out,1);put_u16(out,1);put_u32(out,8000);put_u32(out,16000);put_u16(out,2);put_u16(out,16);
 out.write("data",4);put_u32(out,sizeof(samples));for(short sample:samples)put_u16(out,static_cast<unsigned short>(sample));
}
void write_y4m(const std::filesystem::path& path){
 std::ofstream out(path,std::ios::binary);out<<"YUV4MPEG2 W2 H2 F2:1 Ip A1:1 C444\n";
 const unsigned char frames[2][12]={{16,16,16,16,128,128,128,128,128,128,128,128},{235,235,235,235,128,128,128,128,128,128,128,128}};
 for(const auto& frame:frames){out<<"FRAME\n";out.write(reinterpret_cast<const char*>(frame),sizeof(frame));}
}
bool check_decode(const std::shared_ptr<digitor::VideoFrame>& frame,std::int64_t requested,bool expected,std::size_t decoded_count){
 const bool returned=static_cast<bool>(frame);
 if(returned!=expected){std::cerr<<"decode failure: requested="<<requested<<" returned="<<returned<<" pts="<<(frame?frame->pts:-1)<<" decoded_count="<<decoded_count<<"; demux_eof/flush_sent/decoder_finished are private decoder state\n";return false;}
 return true;
}
}

int main(){
 const auto temporary=std::filesystem::temp_directory_path()/"digitor_media_tests";std::filesystem::create_directories(temporary);
 const auto video_path=temporary/"two_frames.y4m";const auto wav=temporary/"eight_samples.wav";const auto malformed_path=temporary/"malformed.bin";
 write_y4m(video_path);write_wav(wav);{std::ofstream out(malformed_path);out<<"not a media container";}
 CHECK(digitor::ffmpeg_available());
 auto video=digitor::open_video_decoder(video_path.string());
 const auto first=video->decode(0);CHECK(check_decode(first,0,true,first?1:0));
 const auto last=video->decode(1);CHECK(check_decode(last,1,true,last?2:1));
 const auto third=video->decode(2);CHECK(check_decode(third,2,false,2));
 for(digitor::FrameNumber index=3;index<10;++index){const auto eof=video->decode(index);CHECK(check_decode(eof,index,false,2));}
 CHECK(first->width==2&&first->height==2);CHECK(first->pts==0&&last->pts==500000);CHECK(first->pixels.size()==4&&last->pixels.size()==4);
 float first_sum=0,last_sum=0;for(auto p:first->pixels)first_sum+=p.r+p.g+p.b;for(auto p:last->pixels)last_sum+=p.r+p.g+p.b;
 CHECK(first_sum<0.1f&&last_sum>11.9f);
 video->seek(0);const auto sought_first=video->decode(0);const auto sought_last=video->decode(1);const auto sought_eof=video->decode(2);
 CHECK(check_decode(sought_first,0,true,sought_first?1:0));CHECK(check_decode(sought_last,1,true,sought_last?2:1));CHECK(check_decode(sought_eof,2,false,2));
 auto audio=digitor::open_audio_decoder(wav.string());const auto pcm=audio->decode(0);
 CHECK(pcm);CHECK(pcm->sample_rate==8000&&pcm->channels==1&&pcm->samples.size()==8&&pcm->pts==0);
 bool malformed_thrown=false;try{auto unused=digitor::open_video_decoder(malformed_path.string());(void)unused;}catch(const std::exception&){malformed_thrown=true;}CHECK(malformed_thrown);
 std::filesystem::remove_all(temporary);std::cout<<"verified stable EOF, seek reset, y4m/rawvideo, wav/pcm_s16le, and malformed input\n";
}
