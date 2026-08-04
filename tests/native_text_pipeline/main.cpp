#include "digitor/native_text_pipeline.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace digitor;
namespace {
template <std::size_t N>
std::string utf8_string(const char8_t (&text)[N]) {
  return std::string(reinterpret_cast<const char*>(text), N - 1);
}
}
int main(){
  NativeTextPipeline pipeline(128,128);
  ShapingRequest bn;bn.utf8=utf8_string(u8"বাংলা লেখা");bn.primary_font={"Noto Sans Bengali","fixture.ttf",0,400,false};bn.font_size=32;
  const auto shaped=pipeline.shape(bn);
  if(shaped.glyphs.empty()||shaped.script!=TextScript::bengali||!shaped.used_complex_shaping)return 1;
  auto raster=[](const FontDescriptor&,std::uint32_t glyph,double)->std::optional<GlyphBitmap>{GlyphBitmap b;b.width=8;b.height=12;b.advance_x=9;b.coverage.assign(96,static_cast<std::uint8_t>((glyph%200)+55));return b;};
  const auto packet=pipeline.prepare(bn,raster,0xffffffffu);
  if(!packet.gpu_ready||packet.vertices.empty()||packet.indices.size()%6u!=0u)return 2;
  const auto cached=pipeline.cached_glyphs();if(cached==0)return 3;
  const auto packet2=pipeline.prepare(bn,raster);if(packet2.atlas_generation!=packet.atlas_generation||pipeline.cached_glyphs()!=cached)return 4;
  ShapingRequest ar;ar.utf8=utf8_string(u8"العربية");ar.primary_font=bn.primary_font;const auto rtl=pipeline.shape(ar);if(rtl.direction!=TextDirection::right_to_left)return 5;
  bool rejected=false;try{ShapingRequest bad;bad.utf8=std::string("\xC0\xAF",2);pipeline.shape(bad);}catch(const std::invalid_argument&){rejected=true;}if(!rejected)return 6;
  const auto before=pipeline.atlas_generation();pipeline.clear();if(pipeline.atlas_generation()<=before||pipeline.cached_glyphs()!=0)return 7;
  std::cout<<"BENGALI_SCRIPT_DETECTION=1\nRTL_DIRECTION=1\nGLYPH_ATLAS_CACHE=1\nGPU_DRAW_PACKET=1\nUTF8_REJECTION=1\nDEPENDENCIES="<<native_text_dependency_report()<<"\n";
  return 0;
}
