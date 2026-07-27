#include "digitor/lut.hpp"
#include "core/numeric_utils.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace digitor { namespace {
float mix(float a,float b,float t){return a+(b-a)*t;}
Color mixc(Color a,Color b,float t){return{mix(a.r,b.r,t),mix(a.g,b.g,t),mix(a.b,b.b,t),mix(a.a,b.a,t)};}
Color add(Color a,Color b){return{a.r+b.r,a.g+b.g,a.b+b.b,a.a+b.a};} Color scale(Color a,float s){return{a.r*s,a.g*s,a.b*s,a.a*s};}
float channel(const Color& c,int n){return n==0?c.r:n==1?c.g:c.b;}
struct CubeData { std::size_t one{},three{}; Color minimum{0,0,0,1},maximum{1,1,1,1}; std::vector<Color> values; };
CubeData read_cube(std::istream& in){
    CubeData data; std::string line; std::size_t line_number=0;
    while(std::getline(in,line)){
        ++line_number; auto hash=line.find('#'); if(hash!=std::string::npos)line.resize(hash);
        std::istringstream row(line); std::string first; if(!(row>>first))continue;
        if(first=="TITLE"){std::string title;std::getline(row,title);if(title.find_first_not_of(" \t\r")==std::string::npos)throw std::runtime_error("Cube TITLE is empty");continue;}
        if(first=="LUT_1D_SIZE"||first=="LUT_3D_SIZE"){
            std::size_t size{};std::string extra;if(!(row>>size)||(row>>extra)||size<2||size>256)throw std::runtime_error("invalid LUT size at line "+std::to_string(line_number));
            auto&slot=first=="LUT_1D_SIZE"?data.one:data.three;if(slot||data.one||data.three)throw std::runtime_error("Cube must declare exactly one LUT");slot=size;continue;
        }
        if(first=="DOMAIN_MIN"||first=="DOMAIN_MAX"){
            Color value;std::string extra;if(!(row>>value.r>>value.g>>value.b)||(row>>extra))throw std::runtime_error("invalid Cube domain at line "+std::to_string(line_number));
            (first=="DOMAIN_MIN"?data.minimum:data.maximum)=value;continue;
        }
        Color value;std::string extra;std::istringstream values(line);
        if(!(values>>value.r>>value.g>>value.b)||(values>>extra)||!std::isfinite(value.r)||!std::isfinite(value.g)||!std::isfinite(value.b))throw std::runtime_error("invalid Cube row at line "+std::to_string(line_number));
        data.values.push_back(value);
    }
    if(!in.eof()&&in.fail())throw std::runtime_error("failed reading Cube stream");
    if(!data.one&&!data.three)throw std::runtime_error("Cube LUT size is missing");
    const auto expected=data.one?data.one:data.three*data.three*data.three;
    if(data.values.size()!=expected)throw std::runtime_error("Cube data count does not match declared size");
    if(data.minimum.r>=data.maximum.r||data.minimum.g>=data.maximum.g||data.minimum.b>=data.maximum.b)throw std::runtime_error("Cube domain must be increasing");
    return data;
}
}
Lut1D::Lut1D(std::vector<Color> v):values_(std::move(v)){if(values_.size()<2)throw std::invalid_argument("1D LUT requires at least two entries");}
Lut1D Lut1D::load_cube(std::istream& input){auto parsed=parse_cube(input);if(!parsed.one_dimensional)throw std::runtime_error("Cube is not a 1D LUT");return std::move(*parsed.one_dimensional);}
Lut1D Lut1D::load_cube_file(const std::string&path){std::ifstream f(path);if(!f)throw std::runtime_error("cannot open cube file");return load_cube(f);}
Color Lut1D::sample(Color c,LutInterpolation interpolation)const{Color o=c;for(int k=0;k<3;++k){const float domain=channel(domain_max_,k)-channel(domain_min_,k);float x=std::clamp((channel(c,k)-channel(domain_min_,k))/domain,0.f,1.f)*checked_size_to_float(values_.size()-1);std::size_t lo=static_cast<std::size_t>(x),hi=std::min(lo+1,values_.size()-1);float t=interpolation==LutInterpolation::nearest?(x-checked_size_to_float(lo)>=.5f?1.f:0.f):x-checked_size_to_float(lo);float v=mix(channel(values_[lo],k),channel(values_[hi],k),t);if(k==0)o.r=v;else if(k==1)o.g=v;else o.b=v;}return o;}
Lut3D::Lut3D(std::size_t n,std::vector<Color> v):size_(n),values_(std::move(v)){if(n<2||n>256||values_.size()!=n*n*n)throw std::invalid_argument("invalid 3D LUT");}
ParsedCube parse_cube(std::istream& input){auto data=read_cube(input);ParsedCube result;if(data.one){result.one_dimensional.emplace(std::move(data.values));result.one_dimensional->domain_min_=data.minimum;result.one_dimensional->domain_max_=data.maximum;}else{result.three_dimensional.emplace(data.three,std::move(data.values));auto&lut=*result.three_dimensional;lut.domain_min_=data.minimum;lut.domain_max_=data.maximum;}return result;}
Lut3D Lut3D::load_cube(std::istream& in){auto parsed=parse_cube(in);if(!parsed.three_dimensional)throw std::runtime_error("Cube is not a 3D LUT");return std::move(*parsed.three_dimensional);}
Lut3D Lut3D::load_cube_file(const std::string&p){std::ifstream f(p);if(!f)throw std::runtime_error("cannot open cube file");return load_cube(f);}
Color Lut3D::sample(Color c,LutInterpolation mode)const{float p[3];for(int k=0;k<3;++k){float range=channel(domain_max_,k)-channel(domain_min_,k);if(range<=0)throw std::logic_error("invalid LUT domain");p[k]=std::clamp((channel(c,k)-channel(domain_min_,k))/range,0.f,1.f)*checked_size_to_float(size_-1);}std::size_t l[3],h[3];float t[3];for(int k=0;k<3;++k){l[k]=static_cast<std::size_t>(p[k]);h[k]=std::min(l[k]+1,size_-1);t[k]=p[k]-checked_size_to_float(l[k]);if(mode==LutInterpolation::nearest)l[k]=h[k]=(t[k]>=.5f?h[k]:l[k]),t[k]=0;}auto at=[&](std::size_t r,std::size_t g,std::size_t b){return values_[r+size_*(g+size_*b)];};if(mode==LutInterpolation::tetrahedral){Color c000=at(l[0],l[1],l[2]),c100=at(h[0],l[1],l[2]),c010=at(l[0],h[1],l[2]),c001=at(l[0],l[1],h[2]),c110=at(h[0],h[1],l[2]),c101=at(h[0],l[1],h[2]),c011=at(l[0],h[1],h[2]),c111=at(h[0],h[1],h[2]);Color out;if(t[0]>=t[1]){if(t[1]>=t[2])out=add(add(scale(c000,1-t[0]),scale(c100,t[0]-t[1])),add(scale(c110,t[1]-t[2]),scale(c111,t[2])));else if(t[0]>=t[2])out=add(add(scale(c000,1-t[0]),scale(c100,t[0]-t[2])),add(scale(c101,t[2]-t[1]),scale(c111,t[1])));else out=add(add(scale(c000,1-t[2]),scale(c001,t[2]-t[0])),add(scale(c101,t[0]-t[1]),scale(c111,t[1])));}else{if(t[2]>=t[1])out=add(add(scale(c000,1-t[2]),scale(c001,t[2]-t[1])),add(scale(c011,t[1]-t[0]),scale(c111,t[0])));else if(t[2]>=t[0])out=add(add(scale(c000,1-t[1]),scale(c010,t[1]-t[2])),add(scale(c011,t[2]-t[0]),scale(c111,t[0])));else out=add(add(scale(c000,1-t[1]),scale(c010,t[1]-t[0])),add(scale(c110,t[0]-t[2]),scale(c111,t[2])));}out.a=c.a;return out;}
Color low=mixc(mixc(at(l[0],l[1],l[2]),at(h[0],l[1],l[2]),t[0]),mixc(at(l[0],h[1],l[2]),at(h[0],h[1],l[2]),t[0]),t[1]);Color high=mixc(mixc(at(l[0],l[1],h[2]),at(h[0],l[1],h[2]),t[0]),mixc(at(l[0],h[1],h[2]),at(h[0],h[1],h[2]),t[0]),t[1]);Color out=mixc(low,high,t[2]);out.a=c.a;return out;}
void apply_lut_cpu(const Color*i,Color*o,std::size_t n,const Lut1D&l,LutInterpolation m){for(std::size_t k=0;k<n;++k)o[k]=l.sample(i[k],m);}void apply_lut_cpu(const Color*i,Color*o,std::size_t n,const Lut3D&l,LutInterpolation m){for(std::size_t k=0;k<n;++k)o[k]=l.sample(i[k],m);}
void apply_lut_gpu(CommandEncoder&e,const Color*i,Color*o,std::size_t n,const Lut1D&l,LutInterpolation m){e.dispatch([=,&l]{apply_lut_cpu(i,o,n,l,m);});}void apply_lut_gpu(CommandEncoder&e,const Color*i,Color*o,std::size_t n,const Lut3D&l,LutInterpolation m){e.dispatch([=,&l]{apply_lut_cpu(i,o,n,l,m);});}
}
