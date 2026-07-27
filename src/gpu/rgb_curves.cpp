#include "digitor/rgb_curves.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace digitor { namespace {
struct Segment { float x, y, h, delta, m0, m1; };
std::mutex cache_mutex;
std::unordered_map<std::string, std::weak_ptr<const CompiledRgbCurves>> cache;

void append_u32(std::string& s, std::uint32_t v) {
    static constexpr char hex[] = "0123456789abcdef";
    for (int n = 7; n >= 0; --n) s.push_back(hex[(v >> (n * 4)) & 15]);
}
void append_float(std::string& s, float v) { append_u32(s, std::bit_cast<std::uint32_t>(v)); }

std::string key_for(const RgbCurvesParameters& p) {
    std::string s = "rgb-curves:v1;order=master-r-g-b;precision=fp32;lut=";
    append_u32(s, p.lut_size);
    for (const auto* c : {&p.master, &p.red, &p.green, &p.blue}) {
        s.push_back(';'); append_u32(s, c->enabled ? 1u : 0u);
        append_u32(s, static_cast<std::uint32_t>(c->interpolation));
        append_u32(s, static_cast<std::uint32_t>(c->extrapolation));
        append_float(s, c->domain_min); append_float(s, c->domain_max);
        append_u32(s, static_cast<std::uint32_t>(c->points.size()));
        for (auto q : c->points) { append_float(s, q.x); append_float(s, q.y); }
    }
    return s;
}

CompiledRgbCurve compile_one(const RgbCurveDefinition& c, std::uint32_t size) {
    if (c.interpolation != CurveInterpolation::monotone_cubic)
        throw std::invalid_argument("unsupported RGB curve interpolation");
    if (c.extrapolation != CurveExtrapolation::constant && c.extrapolation != CurveExtrapolation::linear)
        throw std::invalid_argument("unsupported RGB curve extrapolation");
    if (!std::isfinite(c.domain_min) || !std::isfinite(c.domain_max) || !(c.domain_min < c.domain_max))
        throw std::invalid_argument("invalid RGB curve domain");
    if (c.points.size() < 2 || c.points.size() > rgb_curve_max_points)
        throw std::invalid_argument("RGB curve point count outside [2,64]");
    for (std::size_t i=0; i<c.points.size(); ++i) {
        const auto q=c.points[i];
        if (!std::isfinite(q.x)||!std::isfinite(q.y)||q.x<c.domain_min||q.x>c.domain_max)
            throw std::invalid_argument("invalid RGB curve point");
        if (i && !(c.points[i-1].x < q.x)) throw std::invalid_argument("RGB curve x positions must be strictly increasing");
    }
    const std::size_t n=c.points.size();
    std::vector<float> h(n-1), d(n-1), m(n);
    for(std::size_t i=0;i+1<n;++i){h[i]=c.points[i+1].x-c.points[i].x;d[i]=(c.points[i+1].y-c.points[i].y)/h[i];}
    // Fritsch-Carlson/PCHIP weighted harmonic interior derivatives.
    m[0]=d[0]; m[n-1]=d[n-2];
    for(std::size_t i=1;i+1<n;++i){
        if(d[i-1]==0.0f||d[i]==0.0f||std::signbit(d[i-1])!=std::signbit(d[i])) m[i]=0.0f;
        else { const float w1=2*h[i]+h[i-1],w2=h[i]+2*h[i-1]; m[i]=(w1+w2)/(w1/d[i-1]+w2/d[i]); }
    }
    auto endpoint=[](float h0,float h1,float d0,float d1){float v=((2*h0+h1)*d0-h0*d1)/(h0+h1);if(std::signbit(v)!=std::signbit(d0))return 0.0f;if(std::signbit(d0)!=std::signbit(d1)&&std::abs(v)>std::abs(3*d0))return 3*d0;return v;};
    if(n>2){m[0]=endpoint(h[0],h[1],d[0],d[1]);m[n-1]=endpoint(h[n-2],h[n-3],d[n-2],d[n-3]);}
    const bool identity=std::all_of(c.points.begin(),c.points.end(),[](auto q){return q.x==q.y;});
    auto eval=[&](float x){
        if(identity) return x;
        if(x<=c.points.front().x)return c.points.front().y+(c.extrapolation==CurveExtrapolation::linear?m.front()*(x-c.points.front().x):0.0f);
        if(x>=c.points.back().x)return c.points.back().y+(c.extrapolation==CurveExtrapolation::linear?m.back()*(x-c.points.back().x):0.0f);
        auto it=std::upper_bound(c.points.begin(),c.points.end(),x,[](float a,RgbCurvePoint b){return a<b.x;});std::size_t i=std::size_t(it-c.points.begin()-1);float t=(x-c.points[i].x)/h[i],t2=t*t,t3=t2*t;
        return (2*t3-3*t2+1)*c.points[i].y+(t3-2*t2+t)*h[i]*m[i]+(-2*t3+3*t2)*c.points[i+1].y+(t3-t2)*h[i]*m[i+1];
    };
    CompiledRgbCurve out; out.domain_min=c.domain_min;out.domain_max=c.domain_max;out.extrapolation=c.extrapolation;out.enabled=c.enabled;out.identity=identity;out.slope_before=m.front();out.slope_after=m.back();out.first_value=eval(c.domain_min);out.last_value=eval(c.domain_max);
    out.samples.resize(size);
    for(std::uint32_t i=0;i<size;++i){float x=c.domain_min+(c.domain_max-c.domain_min)*(static_cast<float>(i)/static_cast<float>(size-1));out.samples[i]=eval(x);if(!std::isfinite(out.samples[i]))throw std::overflow_error("non-finite RGB curve LUT sample");}
    return out;
}
float sample(const CompiledRgbCurve& c,float x) noexcept {
    if(!c.enabled||c.identity)return x;
    if(!std::isfinite(x)) return x; // deterministic propagation of NaN and infinities
    if(x<c.domain_min)return c.extrapolation==CurveExtrapolation::linear?c.first_value+c.slope_before*(x-c.domain_min):c.first_value;
    if(x>c.domain_max)return c.extrapolation==CurveExtrapolation::linear?c.last_value+c.slope_after*(x-c.domain_max):c.last_value;
    float u=(x-c.domain_min)/(c.domain_max-c.domain_min)*static_cast<float>(c.samples.size()-1);auto i=static_cast<std::size_t>(u);if(i+1>=c.samples.size())return c.samples.back();float f=u-static_cast<float>(i);return c.samples[i]+(c.samples[i+1]-c.samples[i])*f;
}
}

std::shared_ptr<const CompiledRgbCurves> CompiledRgbCurves::compile(const RgbCurvesParameters&p){
    if(p.lut_size!=256&&p.lut_size!=1024&&p.lut_size!=4096)throw std::invalid_argument("RGB curve LUT size must be 256, 1024, or 4096");
    const auto key=key_for(p); {std::lock_guard lock(cache_mutex);if(auto it=cache.find(key);it!=cache.end())if(auto hit=it->second.lock())return hit;}
    auto v=std::shared_ptr<CompiledRgbCurves>(new CompiledRgbCurves);v->lut_size_=p.lut_size;v->identity_=key;v->curves_={compile_one(p.master,p.lut_size),compile_one(p.red,p.lut_size),compile_one(p.green,p.lut_size),compile_one(p.blue,p.lut_size)};
    std::lock_guard lock(cache_mutex);auto& slot=cache[key];if(auto hit=slot.lock())return hit;slot=v;return v;
}
Color CompiledRgbCurves::apply(Color c)const noexcept{c.r=sample(curves_[0],c.r);c.g=sample(curves_[0],c.g);c.b=sample(curves_[0],c.b);c.r=sample(curves_[1],c.r);c.g=sample(curves_[2],c.g);c.b=sample(curves_[3],c.b);return c;}
void CompiledRgbCurves::apply(std::span<const Color>a,std::span<Color>b)const{if(a.size()!=b.size())throw std::invalid_argument("RGB curve image sizes differ");for(std::size_t i=0;i<a.size();++i)b[i]=apply(a[i]);}
std::string CompiledRgbCurves::serialize()const{return identity_;}
std::size_t CompiledRgbCurves::cache_size(){std::lock_guard lock(cache_mutex);for(auto i=cache.begin();i!=cache.end();)if(i->second.expired())i=cache.erase(i);else ++i;return cache.size();}
void CompiledRgbCurves::clear_cache(){std::lock_guard lock(cache_mutex);cache.clear();}
GraphPass add_rgb_curves_cpu_pass(RenderGraph&g,GraphResource s,GraphResource d,std::shared_ptr<const CompiledRgbCurves> c,std::span<const Color>i,std::span<Color>o){if(!c)throw std::invalid_argument("null compiled RGB curves");RenderPass p;p.name="RGB Curves CPU Reference";p.reads={{s,ResourceState::shader_read}};p.writes={{d,ResourceState::shader_write}};p.side_effect=false;p.execute=[c=std::move(c),i,o](CommandEncoder&){c->apply(i,o);};return g.add_pass(std::move(p));}
} // namespace digitor
