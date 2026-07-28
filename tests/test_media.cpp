#include "digitor/media.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {
std::filesystem::path fixture_path;

#define CHECK_EQ(expected, actual) do { const auto expected_value=(expected);const auto actual_value=(actual); \
 if(!(expected_value==actual_value)){std::cerr<<"CHECK failed in "<<section<<" at "<<__FILE__<<':'<<__LINE__ \
 <<" fixture="<<fixture_path<<" expected="<<expected_value<<" actual="<<actual_value<<'\n'<<std::flush;return false;}} while(false)
#define CHECK(condition) CHECK_EQ(true,static_cast<bool>(condition))

void begin_section(std::string_view name,const std::filesystem::path& path){
 fixture_path=path;std::cout<<"[media] "<<name<<" fixture="<<path<<'\n'<<std::flush;
}
void put_u16(std::ofstream& out,unsigned value){out.put(static_cast<char>(value));out.put(static_cast<char>(value>>8));}
void put_u32(std::ofstream& out,unsigned value){put_u16(out,value);put_u16(out,value>>16);}
void write_wav(const std::filesystem::path& path){
 const short samples[]={-32768,-20000,-10000,0,10000,20000,30000,32767};
 std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write("RIFF",4);put_u32(out,36+sizeof(samples));out.write("WAVEfmt ",8);
 put_u32(out,16);put_u16(out,1);put_u16(out,1);put_u32(out,8000);put_u32(out,16000);put_u16(out,2);put_u16(out,16);
 out.write("data",4);put_u32(out,sizeof(samples));for(short sample:samples)put_u16(out,static_cast<unsigned short>(sample));
 if(!out)throw std::runtime_error("failed to write WAV fixture: "+path.string());
}
void write_y4m(const std::filesystem::path& path){
 std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<"YUV4MPEG2 W2 H2 F2:1 Ip A1:1 C444\n";
 const unsigned char frames[2][12]={{16,16,16,16,128,128,128,128,128,128,128,128},{235,235,235,235,128,128,128,128,128,128,128,128}};
 for(const auto& frame:frames){out<<"FRAME\n";out.write(reinterpret_cast<const char*>(frame),sizeof(frame));}
 if(!out)throw std::runtime_error("failed to write Y4M fixture: "+path.string());
}
bool validate_fixture(const char* section,const std::filesystem::path& path){
 begin_section(section,path);std::error_code error;const bool exists=std::filesystem::exists(path,error);
 if(error||!exists){std::cerr<<"fixture validation failed at "<<__FILE__<<':'<<__LINE__<<" fixture="<<path<<" expected=exists actual="<<(error?error.message():"missing")<<'\n'<<std::flush;return false;}
 const auto size=std::filesystem::file_size(path,error);if(error||size==0){std::cerr<<"fixture validation failed at "<<__FILE__<<':'<<__LINE__<<" fixture="<<path<<" expected=size>0 actual="<<(error?error.message():std::to_string(size))<<'\n'<<std::flush;return false;}return true;
}
bool run_tests(const std::filesystem::path& directory){
 const auto video_path=directory/"two_frames.y4m";const auto wav_path=directory/"eight_samples.wav";const auto malformed_path=directory/"malformed.bin";
 const char* section="fixture validation";
 begin_section("fixture generation",directory);write_y4m(video_path);write_wav(wav_path);
 CHECK(validate_fixture("Y4M fixture validation",video_path));CHECK(validate_fixture("WAV fixture validation",wav_path));CHECK(validate_fixture("malformed fixture validation",malformed_path));
 section="FFmpeg availability";begin_section(section,video_path);CHECK(digitor::ffmpeg_available());
 const auto generated_mp4=directory/"video.mp4";
 if(std::filesystem::exists(generated_mp4)){
  for(const char* name:{"video.mp4","video.mov","video.mkv"}){
   const auto container=directory/name;CHECK(validate_fixture("generated container",container));
   section="container stream discovery and decode";begin_section(section,container);
   auto decoder=digitor::open_video_decoder(container.string());const auto decoded=decoder->decode(0);
   CHECK(decoded);CHECK(decoded->width>0);CHECK(decoded->height>0);CHECK(!decoded->pixels.empty());
   CHECK_EQ(static_cast<std::size_t>(decoded->width)*decoded->height,decoded->pixels.size());
   decoder->seek(0);CHECK(decoder->decode(0));
  }
  const auto generated_audio=directory/"audio.wav";CHECK(validate_fixture("generated audio",generated_audio));
  auto generated_audio_decoder=digitor::open_audio_decoder(generated_audio.string());
  const auto generated_pcm=generated_audio_decoder->decode(0);CHECK(generated_pcm);CHECK(!generated_pcm->samples.empty());
 }else{
  std::cout<<"[media] optional CLI-generated MP4/MOV/MKV/WAV fixtures unavailable; "
             "running repository-owned Y4M/WAV fixtures\n"<<std::flush;
 }
 section="video decode and stable EOF";begin_section(section,video_path);auto video=digitor::open_video_decoder(video_path.string());
 const auto first=video->decode(0);CHECK(first);const auto last=video->decode(1);CHECK(last);const auto third=video->decode(2);CHECK_EQ(false,static_cast<bool>(third));
 for(digitor::FrameNumber index=3;index<10;++index){const auto eof=video->decode(index);if(eof){std::cerr<<"decode failure in "<<section<<" at "<<__FILE__<<':'<<__LINE__<<" fixture="<<fixture_path<<" requested="<<index<<" expected=false actual=true pts="<<eof->pts<<" decoded_count=2\n"<<std::flush;return false;}}
 CHECK_EQ(2u,first->width);CHECK_EQ(2u,first->height);CHECK_EQ(0,first->pts);CHECK_EQ(500000,last->pts);CHECK_EQ(4u,first->pixels.size());CHECK_EQ(4u,last->pixels.size());
 CHECK_EQ(first.get(),video->decode(0).get());
 float first_sum=0,last_sum=0;for(auto p:first->pixels)first_sum+=p.r+p.g+p.b;for(auto p:last->pixels)last_sum+=p.r+p.g+p.b;CHECK(first_sum<0.1f);CHECK(last_sum>11.9f);
 section="seek after EOF";begin_section(section,video_path);video->seek(0);const auto sought_first=video->decode(0);CHECK(sought_first);const auto sought_last=video->decode(1);CHECK(sought_last);const auto sought_eof=video->decode(2);CHECK_EQ(false,static_cast<bool>(sought_eof));
 section="timestamp seek";begin_section(section,video_path);video->seek(500000);const auto timestamp_sought=video->decode(0);CHECK(timestamp_sought);CHECK_EQ(500000,timestamp_sought->pts);CHECK_EQ(last_sum,[&]{float sum=0;for(auto p:timestamp_sought->pixels)sum+=p.r+p.g+p.b;return sum;}());
 section="audio decode";begin_section(section,wav_path);auto audio=digitor::open_audio_decoder(wav_path.string());const auto pcm=audio->decode(0);CHECK(pcm);CHECK_EQ(8000u,pcm->sample_rate);CHECK_EQ(1u,pcm->channels);CHECK_EQ(8u,pcm->samples.size());CHECK_EQ(0,pcm->pts);
 section="malformed input";begin_section(section,malformed_path);bool malformed_thrown=false;try{auto unused=digitor::open_video_decoder(malformed_path.string());(void)unused;}catch(const std::exception& error){malformed_thrown=true;std::cout<<"[media] expected FFmpeg failure: "<<error.what()<<'\n'<<std::flush;}CHECK(malformed_thrown);
 return true;
}
}

int main(){
 try{const auto directory=std::filesystem::current_path()/"digitor_media_fixtures";std::filesystem::create_directories(directory);return run_tests(directory)?0:1;}
 catch(const std::exception& error){std::cerr<<"unhandled media test exception at "<<__FILE__<<':'<<__LINE__<<" fixture="<<fixture_path<<" error="<<error.what()<<'\n'<<std::flush;return 1;}
 catch(...){std::cerr<<"unhandled non-standard media test exception at "<<__FILE__<<':'<<__LINE__<<" fixture="<<fixture_path<<'\n'<<std::flush;return 1;}
}
