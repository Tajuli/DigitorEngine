#include "digitor/log_wheels.hpp"
#include "digitor/cpu_parallel_executor.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace digitor { namespace {
std::atomic_uint64_t reference_calls{};
constexpr std::size_t kLogWheelGrain = 16U * 1024U;

void u32(std::string& out, std::uint32_t value) {
  constexpr char h[] = "0123456789abcdef";
  for (unsigned byte = 0; byte != 4; ++byte) {
    const auto v = (value >> (byte * 8)) & 0xffu;
    out.push_back(h[v >> 4]); out.push_back(h[v & 15]);
  }
}
void fp(std::string& out, float value) { u32(out, std::bit_cast<std::uint32_t>(value)); }
void validate_control(const LogWheelControl& c, const char* name) {
  const auto valid = [](float v, float lo, float hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
  };
  if (!valid(c.rgb.r, -2, 2) || !valid(c.rgb.g, -2, 2) ||
      !valid(c.rgb.b, -2, 2) || !valid(c.master, -8, 8))
    throw std::invalid_argument(std::string("log wheels invalid ") + name);
}
std::string encode(const LogWheelsDescriptor& p) {
  std::string s = "log-wheels:"; u32(s, p.schema_version);
  const auto wheel = [&](const LogWheelControl& c) {
    fp(s,c.rgb.r); fp(s,c.rgb.g); fp(s,c.rgb.b); fp(s,c.master);
    u32(s,c.enabled ? 1u : 0u);
  };
  wheel(p.shadows); wheel(p.midtones); wheel(p.highlights); wheel(p.global);
  fp(s,p.shadow_pivot); fp(s,p.highlight_pivot); fp(s,p.transition_width);
  return s;
}
float smoothstep(float edge0, float edge1, float x) noexcept {
  if (edge0 == edge1) return x < edge0 ? 0.0f : 1.0f;
  const float t = std::clamp((x-edge0)/(edge1-edge0),0.0f,1.0f);
  return t*t*(3.0f-2.0f*t);
}
float luminance(Color c) noexcept {
  return c.r*0.2126f + c.g*0.7152f + c.b*0.0722f;
}
struct Weights { float shadows, midtones, highlights; };
Weights weights(float y,const LogWheelsDescriptor&p) noexcept {
  const float half=p.transition_width*0.5f;
  const float s=1.0f-smoothstep(p.shadow_pivot-half,p.shadow_pivot+half,y);
  const float h=smoothstep(p.highlight_pivot-half,p.highlight_pivot+half,y);
  return {s,std::max(0.0f,1.0f-s-h),h};
}
float channel(float x,float srgb,float mrgb,float hrgb,float grgb,
              const LogWheelsDescriptor&p,Weights w) noexcept {
  if (!std::isfinite(x)) return x;
  const float stop = (p.shadows.enabled?p.shadows.master*w.shadows:0.0f)+
                     (p.midtones.enabled?p.midtones.master*w.midtones:0.0f)+
                     (p.highlights.enabled?p.highlights.master*w.highlights:0.0f)+
                     (p.global.enabled?p.global.master:0.0f);
  const float balance=(p.shadows.enabled?srgb*w.shadows:0.0f)+
                      (p.midtones.enabled?mrgb*w.midtones:0.0f)+
                      (p.highlights.enabled?hrgb*w.highlights:0.0f)+
                      (p.global.enabled?grgb:0.0f);
  return x*std::exp2(stop)+balance;
}
} // namespace

LogWheelsParameters::LogWheelsParameters(LogWheelsDescriptor p)
    : values_(p),serialization_(encode(p)),identity_(serialization_) {}
std::shared_ptr<const LogWheelsParameters> LogWheelsParameters::create(
    const LogWheelsDescriptor&p) {
  if(p.schema_version!=log_wheels_parameter_version)
    throw std::invalid_argument("unsupported log wheels schema version");
  validate_control(p.shadows,"shadows"); validate_control(p.midtones,"midtones");
  validate_control(p.highlights,"highlights"); validate_control(p.global,"global");
  if(!std::isfinite(p.shadow_pivot)||!std::isfinite(p.highlight_pivot)||
     !std::isfinite(p.transition_width)||p.shadow_pivot<0||p.shadow_pivot>1||
     p.highlight_pivot<0||p.highlight_pivot>1||p.shadow_pivot>=p.highlight_pivot||
     p.transition_width<=0||p.transition_width>1)
    throw std::invalid_argument("log wheels invalid tonal partition");
  return std::shared_ptr<const LogWheelsParameters>(new LogWheelsParameters(p));
}
bool LogWheelsParameters::is_identity()const noexcept {
  const auto identity=[](const LogWheelControl&c){return !c.enabled||
    (c.rgb.r==0&&c.rgb.g==0&&c.rgb.b==0&&c.master==0);};
  return identity(values_.shadows)&&identity(values_.midtones)&&
         identity(values_.highlights)&&identity(values_.global);
}
Color apply_log_wheels_reference(Color c,const LogWheelsParameters&parameters)noexcept{
  reference_calls.fetch_add(1,std::memory_order_relaxed);
  const auto&p=parameters.values();const float alpha=c.a;
  if(!std::isfinite(c.r)||!std::isfinite(c.g)||!std::isfinite(c.b)){
    c.a=alpha;return c;
  }
  const auto w=weights(luminance(c),p);
  c.r=channel(c.r,p.shadows.rgb.r,p.midtones.rgb.r,p.highlights.rgb.r,p.global.rgb.r,p,w);
  c.g=channel(c.g,p.shadows.rgb.g,p.midtones.rgb.g,p.highlights.rgb.g,p.global.rgb.g,p,w);
  c.b=channel(c.b,p.shadows.rgb.b,p.midtones.rgb.b,p.highlights.rgb.b,p.global.rgb.b,p,w);
  c.a=alpha;return c;
}
void apply_log_wheels_reference(std::span<const Color>in,std::span<Color>out,const LogWheelsParameters&p){
  if(in.size()!=out.size())throw std::invalid_argument("log wheels image sizes differ");
  shared_cpu_executor().parallel_for(in.size(), kLogWheelGrain,
    [&](std::size_t begin,std::size_t end){
      for(std::size_t i=begin;i<end;++i)out[i]=apply_log_wheels_reference(in[i],p);
    });
}
std::uint64_t log_wheels_reference_count()noexcept{return reference_calls.load(std::memory_order_relaxed);}
void reset_log_wheels_reference_count()noexcept{reference_calls.store(0,std::memory_order_relaxed);}
GraphPass add_log_wheels_cpu_pass(RenderGraph&g,GraphResource s,GraphResource d,std::shared_ptr<const LogWheelsParameters>p,std::span<const Color>i,std::span<Color>o){
  if(!p) throw std::invalid_argument("null log wheels parameters");
  RenderPass pass;
  pass.name="Log Wheels CPU Reference";
  pass.reads={{s,ResourceState::shader_read}};
  pass.writes={{d,ResourceState::shader_write}};
  pass.execute=[p=std::move(p),i,o](CommandEncoder&){apply_log_wheels_reference(i,o,*p);};
  return g.add_pass(std::move(pass));}
GraphPass add_log_wheels_pass(RenderGraph&g,GraphResource s,GraphResource d,std::function<void(CommandEncoder&)>e){
  if(!e) throw std::invalid_argument("log wheels native executor is required");
  RenderPass p;
  p.name="Log Wheels Native";
  p.reads={{s,ResourceState::shader_read}};
  p.writes={{d,ResourceState::shader_write}};
  p.execute=std::move(e);
  return g.add_pass(std::move(p));}
} // namespace digitor
