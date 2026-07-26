#include "digitor/media.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void put_u16(std::ofstream& out,unsigned value){out.put(static_cast<char>(value));out.put(static_cast<char>(value>>8));}
void put_u32(std::ofstream& out,unsigned value){put_u16(out,value);put_u16(out,value>>16);}
void write_wav(const std::filesystem::path& path){
 const short samples[]={-32768,-20000,-10000,0,10000,20000,30000,32767};
 std::ofstream out(path,std::ios::binary);out.write("RIFF",4);put_u32(out,36+sizeof(samples));out.write("WAVEfmt ",8);
 put_u32(out,16);put_u16(out,1);put_u16(out,1);put_u32(out,8000);put_u32(out,16000);put_u16(out,2);put_u16(out,16);
 out.write("data",4);put_u32(out,sizeof(samples));for(short sample:samples)put_u16(out,static_cast<unsigned short>(sample));
}
}

int main(){
 const auto root=std::filesystem::path(DIGITOR_FIXTURE_DIR);
 const auto temporary=std::filesystem::temp_directory_path()/"digitor_media_tests";std::filesystem::create_directories(temporary);
 const auto wav=temporary/"eight_samples.wav";const auto malformed=temporary/"malformed.bin";write_wav(wav);
 {std::ofstream out(malformed);out<<"not a media container";}
 assert(digitor::ffmpeg_available());
 auto video=digitor::open_video_decoder((root/"two_frames.y4m").string());
 auto first=video->decode(0);auto last=video->decode(1);
 assert(first&&last&&!video->decode(2));assert(first->width==2&&first->height==2);
 assert(first->pts==0&&last->pts==500000);assert(first->pixels.size()==4&&last->pixels.size()==4);
 float first_sum=0,last_sum=0;for(auto p:first->pixels)first_sum+=p.r+p.g+p.b;for(auto p:last->pixels)last_sum+=p.r+p.g+p.b;
 assert(first_sum<0.1f&&last_sum>11.9f); // deterministic first/last pixel checksums
 video->seek(0);auto sought=video->decode(0);assert(sought&&sought->pts==0&&sought->pixels.size()==4);
 auto audio=digitor::open_audio_decoder(wav.string());auto pcm=audio->decode(0);
 assert(pcm&&pcm->sample_rate==8000&&pcm->channels==1&&pcm->samples.size()==8&&pcm->pts==0);
 bool malformed=false;try{digitor::open_video_decoder(malformed.string());}catch(const std::exception&){malformed=true;}assert(malformed);
 std::filesystem::remove_all(temporary);
 std::cout<<"verified y4m/rawvideo and wav/pcm_s16le fixtures\n";
}
