#include "digitor/color_science.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace digitor::color {
namespace {
constexpr double kEpsilon = 1e-12;
void finite(double x) { if (!std::isfinite(x)) throw std::invalid_argument("non-finite color value"); }
double signed_pow(double x, double p) { return std::copysign(std::pow(std::abs(x), p), x); }
Vec3 white_xyz(Chromaticity w) {
  finite(w.x); finite(w.y);
  if (w.x <= 0 || w.y <= 0 || w.x + w.y >= 1) throw std::invalid_argument("invalid white chromaticity");
  return {w.x / w.y, 1.0, (1.0 - w.x - w.y) / w.y};
}
Mat3 diagonal(Vec3 x) { return {{{x.x,0,0, 0,x.y,0, 0,0,x.z}}}; }
double coefficient(MatrixCoefficients m) {
  switch (m) { case MatrixCoefficients::bt601:return .299; case MatrixCoefficients::bt709:return .2126;
    case MatrixCoefficients::bt2020_ncl:return .2627; default:throw std::invalid_argument("YUV matrix is not specified"); }
}
double kb(MatrixCoefficients m) { switch(m) {case MatrixCoefficients::bt601:return .114;case MatrixCoefficients::bt709:return .0722;case MatrixCoefficients::bt2020_ncl:return .0593;default:throw std::invalid_argument("YUV matrix is not specified");} }
}

MetadataResolution resolve_metadata(const Metadata& in, const Metadata& fallback) {
  MetadataResolution r{in, false, {}};
#define RESOLVE(field, unspecified) if (r.metadata.field == unspecified) { if (fallback.field == unspecified) throw std::invalid_argument("unresolved color metadata: " #field); r.metadata.field=fallback.field; r.used_fallback=true; r.decision += (r.decision.empty()?"":",") + std::string(#field); }
  RESOLVE(pixel_format, PixelFormat::unknown); RESOLVE(primaries, Primaries::unspecified);
  RESOLVE(transfer, Transfer::unspecified); RESOLVE(matrix, MatrixCoefficients::unspecified);
  RESOLVE(range, Range::unspecified); RESOLVE(chroma_location, ChromaLocation::unspecified);
  RESOLVE(alpha, Alpha::unspecified); RESOLVE(source, Space::unknown);
  RESOLVE(working, Space::unknown); RESOLVE(output, Space::unknown);
#undef RESOLVE
  if (!r.metadata.bit_depth) { if (!fallback.bit_depth) throw std::invalid_argument("unresolved color metadata: bit_depth"); r.metadata.bit_depth=fallback.bit_depth;r.used_fallback=true;r.decision+=(r.decision.empty()?"":",")+std::string("bit_depth"); }
  return r;
}

float decode(Transfer t, float x) {
  finite(x);
  double v=x, y{};
  switch(t) {
    case Transfer::linear: y=v; break;
    case Transfer::srgb: y=v <= .04045 && v >= -.04045 ? v/12.92 : signed_pow((std::abs(v)+.055)/1.055,2.4); break;
    case Transfer::bt709: y=std::abs(v)<=.081 ? v/4.5 : std::copysign(std::pow((std::abs(v)+.099)/1.099,1/.45),v); break;
    case Transfer::bt1886: case Transfer::gamma24:y=signed_pow(v,2.4);break;
    case Transfer::gamma22:y=signed_pow(v,2.2);break;
    case Transfer::pq: { if(v<0)throw std::domain_error("PQ is defined for non-negative signals");constexpr double m1=2610.0/16384,m2=2523.0/32,c1=3424.0/4096,c2=2413.0/128,c3=2392.0/128;double p=std::pow(v,1/m2);y=std::pow(std::max(p-c1,0.0)/(c2-c3*p),1/m1);break; }
    case Transfer::hlg: { if(v<0)throw std::domain_error("HLG is defined for non-negative signals");constexpr double a=.17883277,b=1-4*a,c=.55991073;y=v<=.5?v*v/3:(std::exp((v-c)/a)+b)/12;break; }
    default: throw std::invalid_argument("unspecified transfer function");
  } return static_cast<float>(y);
}
float encode(Transfer t, float x) {
  finite(x); double v=x,y{};
  switch(t) {
    case Transfer::linear:y=v;break;
    case Transfer::srgb:y=std::abs(v)<=.0031308?12.92*v:std::copysign(1.055*std::pow(std::abs(v),1/2.4)-.055,v);break;
    case Transfer::bt709:y=std::abs(v)<=.018?4.5*v:std::copysign(1.099*std::pow(std::abs(v),.45)-.099,v);break;
    case Transfer::bt1886:case Transfer::gamma24:y=signed_pow(v,1/2.4);break;
    case Transfer::gamma22:y=signed_pow(v,1/2.2);break;
    case Transfer::pq:{if(v<0)throw std::domain_error("PQ is defined for non-negative luminance");constexpr double m1=2610.0/16384,m2=2523.0/32,c1=3424.0/4096,c2=2413.0/128,c3=2392.0/128;double p=std::pow(v,m1);y=std::pow((c1+c2*p)/(1+c3*p),m2);break;}
    case Transfer::hlg:{if(v<0)throw std::domain_error("HLG is defined for non-negative luminance");constexpr double a=.17883277,b=1-4*a,c=.55991073;y=v<=1.0/12?std::sqrt(3*v):a*std::log(12*v-b)+c;break;}
    default:throw std::invalid_argument("unspecified transfer function");
  } return static_cast<float>(y);
}

Chromaticities chromaticities(Primaries p) { constexpr Chromaticity d65{.3127,.3290}; switch(p){case Primaries::bt709:return{{.64,.33},{.30,.60},{.15,.06},d65};case Primaries::display_p3:return{{.68,.32},{.265,.690},{.150,.060},d65};case Primaries::bt2020:return{{.708,.292},{.170,.797},{.131,.046},d65};case Primaries::bt601_525:return{{.630,.340},{.310,.595},{.155,.070},d65};case Primaries::bt601_625:return{{.640,.330},{.290,.600},{.150,.060},d65};default:throw std::invalid_argument("unspecified primaries");} }
Vec3 multiply(const Mat3&a,Vec3 x){return{a.v[0]*x.x+a.v[1]*x.y+a.v[2]*x.z,a.v[3]*x.x+a.v[4]*x.y+a.v[5]*x.z,a.v[6]*x.x+a.v[7]*x.y+a.v[8]*x.z};}
Mat3 multiply(const Mat3&a,const Mat3&b){Mat3 r{};for(int i=0;i<3;i++)for(int j=0;j<3;j++)for(int k=0;k<3;k++)r.v[i*3+j]+=a.v[i*3+k]*b.v[k*3+j];return r;}
Mat3 inverse(const Mat3&m){auto&a=m.v;double d=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);if(!std::isfinite(d)||std::abs(d)<kEpsilon)throw std::invalid_argument("singular color matrix");return{{{(a[4]*a[8]-a[5]*a[7])/d,(a[2]*a[7]-a[1]*a[8])/d,(a[1]*a[5]-a[2]*a[4])/d,(a[5]*a[6]-a[3]*a[8])/d,(a[0]*a[8]-a[2]*a[6])/d,(a[2]*a[3]-a[0]*a[5])/d,(a[3]*a[7]-a[4]*a[6])/d,(a[1]*a[6]-a[0]*a[7])/d,(a[0]*a[4]-a[1]*a[3])/d}}};}
Mat3 rgb_to_xyz(const Chromaticities&p){auto xyz=[](Chromaticity c){finite(c.x);finite(c.y);if(std::abs(c.y)<kEpsilon)throw std::invalid_argument("invalid primary chromaticity");return Vec3{c.x/c.y,1,(1-c.x-c.y)/c.y};};auto r=xyz(p.red),g=xyz(p.green),b=xyz(p.blue);Mat3 base{{{r.x,g.x,b.x,r.y,g.y,b.y,r.z,g.z,b.z}}};auto s=multiply(inverse(base),white_xyz(p.white));return multiply(base,diagonal(s));}
Mat3 rgb_to_rgb(const Chromaticities&s,const Chromaticities&d){return multiply(inverse(rgb_to_xyz(d)),rgb_to_xyz(s));}
Mat3 adaptation_matrix(Adaptation a,Chromaticity sw,Chromaticity dw){if(a!=Adaptation::bradford)throw std::invalid_argument("unsupported adaptation");if(sw.x==dw.x&&sw.y==dw.y)return{{{1,0,0,0,1,0,0,0,1}}};Mat3 b{{{.8951,.2664,-.1614,-.7502,1.7135,.0367,.0389,-.0685,1.0296}}};auto s=multiply(b,white_xyz(sw)),d=multiply(b,white_xyz(dw));return multiply(multiply(inverse(b),diagonal({d.x/s.x,d.y/s.y,d.z/s.z})),b);}

Vec3 yuv_to_rgb(YuvCode q,MatrixCoefficients m,Range range,unsigned bits){if(bits<8||bits>16||range==Range::unspecified)throw std::invalid_argument("invalid YUV format");const double scale=std::ldexp(1.0,static_cast<int>(bits)-8),max=std::ldexp(1.0,static_cast<int>(bits))-1;double y,u,v;if(range==Range::limited){y=(q.y-16*scale)/(219*scale);u=(q.u-128*scale)/(224*scale);v=(q.v-128*scale)/(224*scale);}else{y=q.y/max;double center=std::ldexp(1.0,static_cast<int>(bits)-1);u=(q.u-center)/max;v=(q.v-center)/max;}double kr=coefficient(m),blue=kb(m),kg=1-kr-blue;return{y+2*(1-kr)*v,y-2*blue*(1-blue)/kg*u-2*kr*(1-kr)/kg*v,y+2*(1-blue)*u};}
Vec3 tone_map(Vec3 x,ToneMap op){finite(x.x);finite(x.y);finite(x.z);if(op==ToneMap::passthrough)return x;if(op==ToneMap::diagnostic_hard_clip)return{std::clamp(x.x,0.,1.),std::clamp(x.y,0.,1.),std::clamp(x.z,0.,1.)};if(op==ToneMap::reinhard)return{x.x/(1+x.x),x.y/(1+x.y),x.z/(1+x.z)};throw std::invalid_argument("tone map");}

TransformGraph TransformGraph::compile(std::span<const Stage>s){TransformGraph g;g.stages_.assign(s.begin(),s.end());int last=-1;std::uint64_t h=1469598103934665603ull;for(auto&stage:g.stages_){int rank=static_cast<int>(stage.kind);if(rank<last)throw std::invalid_argument("invalid color stage order");last=rank;auto mix=[&](std::uint64_t x){for(int i=0;i<8;i++){h^=(x>>(i*8))&255;h*=1099511628211ull;}};mix(static_cast<unsigned>(stage.kind));for(double x:stage.parameters){finite(x);mix(std::bit_cast<std::uint64_t>(x));}}g.identity_=h;return g;}
Color TransformGraph::execute(Color c)const{for(auto&s:stages_){if(s.kind==StageKind::decode_transfer||s.kind==StageKind::encode_transfer){auto t=static_cast<Transfer>(static_cast<int>(s.parameters[0]));auto fn=s.kind==StageKind::decode_transfer?decode:encode;c.r=fn(t,c.r);c.g=fn(t,c.g);c.b=fn(t,c.b);}else if(s.kind==StageKind::primaries_conversion||s.kind==StageKind::chromatic_adaptation||s.kind==StageKind::working_conversion||s.kind==StageKind::output_primaries_conversion){Vec3 v=multiply(Mat3{s.parameters},Vec3{c.r,c.g,c.b});c.r=static_cast<float>(v.x);c.g=static_cast<float>(v.y);c.b=static_cast<float>(v.z);}else if(s.kind==StageKind::tone_map){auto v=tone_map({c.r,c.g,c.b},static_cast<ToneMap>(static_cast<int>(s.parameters[0])));c.r=static_cast<float>(v.x);c.g=static_cast<float>(v.y);c.b=static_cast<float>(v.z);}else throw std::invalid_argument("stage has no CPU executor");}return c;}
FutureToolContract future_tool_contract(FutureTool t){switch(t){case FutureTool::rgb_curves:return{{},{},StageKind::color_operation,true,true,true,"normalized control points; domain explicitly serialized"};case FutureTool::primary_wheels:return{{},{},StageKind::color_operation,true,true,true,"lift/gamma/gain ranges to be versioned"};case FutureTool::log_wheels:return{{},{},StageKind::color_operation,true,true,true,"ranges and tonal pivots to be versioned"};case FutureTool::hsl_qualifier:return{{},{},StageKind::color_operation,true,true,true,"normalized hue/saturation/luminance bounds"};case FutureTool::lut:return{{},{},StageKind::color_operation,true,true,true,"validated 1D/3D domain and lattice"};}throw std::invalid_argument("future tool");}
} // namespace digitor::color
