#include "digitor/color_science.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
bool near(double a,double b,double e=2e-6){return std::abs(a-b)<=e;}
}
void test_color_science(){using namespace digitor;using namespace digitor::color;
 assert(decode(Transfer::srgb,.04045f)==.04045f/12.92f);
 assert(near(encode(Transfer::srgb,.0031308f),.040449936));
 assert(near(decode(Transfer::bt709,.080999f),.080999/4.5));
 assert(near(decode(Transfer::bt709,.081001f),std::pow((.081001+.099)/1.099,1/.45)));
 for(auto t:{Transfer::linear,Transfer::srgb,Transfer::bt709,Transfer::gamma22,Transfer::gamma24,Transfer::pq,Transfer::hlg})for(float v:{0.f,.001f,.18f,.5f,1.f})assert(near(decode(t,encode(t,v)),v,8e-6));
 assert(near(encode(Transfer::pq,.01f),.5080784,2e-6)); // ST 2084: 100 cd/m2 / 10000 cd/m2
 assert(near(decode(Transfer::hlg,.5f),1.0/12,2e-7));
 bool rejected=false;try{decode(Transfer::srgb,std::numeric_limits<float>::quiet_NaN());}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 rejected=false;try{decode(Transfer::pq,-.1f);}catch(const std::domain_error&){rejected=true;}assert(rejected);

 auto m=rgb_to_xyz(chromaticities(Primaries::bt709));
 assert(near(m.v[0],.4123907993,1e-9)&&near(m.v[4],.7151686788,1e-9)&&near(m.v[8],.9505321522,1e-9));
 Vec3 sample{-.1,.42,1.25};auto round=multiply(inverse(m),multiply(m,sample));assert(near(round.x,sample.x,1e-12)&&near(round.y,sample.y,1e-12)&&near(round.z,sample.z,1e-12));
 auto p3=rgb_to_rgb(chromaticities(Primaries::display_p3),chromaticities(Primaries::bt709));auto red=multiply(p3,Vec3{1,0,0});assert(near(red.x,1.224940176,1e-8)&&near(red.y,-.042056955,1e-8));
 auto id=adaptation_matrix(Adaptation::bradford,{.3127,.3290},{.3127,.3290});assert(id.v[0]==1&&id.v[4]==1&&id.v[8]==1);
 auto d50=adaptation_matrix(Adaptation::bradford,{.3127,.3290},{.34567,.35850});auto w=multiply(d50,Vec3{.9504559271,1,1.0890577508});assert(near(w.x,.964211994,2e-7)&&near(w.z,.825188285,2e-7));

 auto black=yuv_to_rgb({16,128,128},MatrixCoefficients::bt709,Range::limited,8);assert(near(black.x,0)&&near(black.y,0)&&near(black.z,0));
 auto white=yuv_to_rgb({235,128,128},MatrixCoefficients::bt601,Range::limited,8);assert(near(white.x,1)&&near(white.y,1)&&near(white.z,1));
 auto full=yuv_to_rgb({0,128,128},MatrixCoefficients::bt2020_ncl,Range::full,8);assert(near(full.x,0)&&near(full.y,0)&&near(full.z,0));

 Metadata unknown{},fallback{};fallback.pixel_format=PixelFormat::rgba32_float;fallback.bit_depth=32;fallback.primaries=Primaries::bt709;fallback.transfer=Transfer::linear;fallback.matrix=MatrixCoefficients::identity;fallback.range=Range::full;fallback.chroma_location=ChromaLocation::top_left;fallback.alpha=Alpha::straight;fallback.source=Space::linear_bt709;fallback.working=Space::linear_bt709;fallback.output=Space::linear_bt709;auto resolved=resolve_metadata(unknown,fallback);assert(resolved.used_fallback&&resolved.decision.find("primaries")!=std::string::npos);
 Stage stages[]{{StageKind::decode_transfer,{static_cast<double>(Transfer::srgb)}},{StageKind::primaries_conversion,m.v},{StageKind::encode_transfer,{static_cast<double>(Transfer::srgb)}}};auto a=TransformGraph::compile(stages),b=TransformGraph::compile(stages);assert(a.identity()==b.identity());
 rejected=false;try{Stage bad[]{{StageKind::encode_transfer,{}},{StageKind::decode_transfer,{}}};(void)TransformGraph::compile(bad);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 auto pass=tone_map({-.2,.5,2},ToneMap::passthrough);assert(pass.x==-.2&&pass.z==2);auto clip=tone_map({-.2,.5,2},ToneMap::diagnostic_hard_clip);assert(clip.x==0&&clip.z==1);
 assert(future_tool_contract(FutureTool::lut).cpu_gpu_parity_required);
}
