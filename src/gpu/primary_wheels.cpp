#include "digitor/primary_wheels.hpp"

#include <atomic>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace digitor { namespace {
std::atomic_uint64_t reference_calls{};

void u32(std::string& out, std::uint32_t value) {
  // Fixed little-endian wire representation, hex encoded for safe storage.
  constexpr char h[] = "0123456789abcdef";
  for (unsigned byte = 0; byte != 4; ++byte) {
    const auto v = (value >> (byte * 8)) & 0xffu;
    out.push_back(h[v >> 4]); out.push_back(h[v & 15]);
  }
}
void fp(std::string& out, float value) { u32(out, std::bit_cast<std::uint32_t>(value)); }
void validate_rgb(PrimaryRgb v, float lo, float hi, const char* name) {
  if (!std::isfinite(v.r) || !std::isfinite(v.g) || !std::isfinite(v.b) ||
      v.r < lo || v.r > hi || v.g < lo || v.g > hi || v.b < lo || v.b > hi)
    throw std::invalid_argument(std::string("primary wheels invalid ") + name);
}
std::string encode(const PrimaryWheelsDescriptor& p) {
  std::string s = "primary-wheels:"; u32(s, p.schema_version);
  const auto wheel = [&](PrimaryRgb rgb, float master, bool enabled) {
    fp(s,rgb.r); fp(s,rgb.g); fp(s,rgb.b); fp(s,master); u32(s,enabled ? 1u : 0u);
  };
  wheel(p.lift,p.lift_master,p.lift_enabled); wheel(p.gamma,p.gamma_master,p.gamma_enabled);
  wheel(p.gain,p.gain_master,p.gain_enabled); wheel(p.offset,p.offset_master,p.offset_enabled);
  return s;
}
float signed_pow(float x, float exponent) noexcept {
  if (!std::isfinite(x)) return x;
  return std::copysign(std::pow(std::abs(x), exponent), x);
}
float channel(float x, float lift, float gamma, float gain, float offset,
              const PrimaryWheelsDescriptor& p) noexcept {
  if (!std::isfinite(x)) return x;
  if (p.lift_enabled) x += lift + p.lift_master;
  if (p.gamma_enabled) x = signed_pow(x, 1.0f / (gamma * p.gamma_master));
  if (p.gain_enabled) x *= gain * p.gain_master;
  if (p.offset_enabled) x += offset + p.offset_master;
  return x;
}
} // namespace

PrimaryWheelsParameters::PrimaryWheelsParameters(PrimaryWheelsDescriptor p)
    : values_(p), serialization_(encode(p)), identity_(serialization_) {}
std::shared_ptr<const PrimaryWheelsParameters> PrimaryWheelsParameters::create(
    const PrimaryWheelsDescriptor& p) {
  if (p.schema_version != primary_wheels_parameter_version)
    throw std::invalid_argument("unsupported primary wheels schema version");
  validate_rgb(p.lift,-4,4,"lift RGB"); validate_rgb(p.offset,-4,4,"offset RGB");
  validate_rgb(p.gamma,.01f,10,"gamma RGB"); validate_rgb(p.gain,0,16,"gain RGB");
  if (!std::isfinite(p.lift_master)||p.lift_master < -4||p.lift_master > 4 ||
      !std::isfinite(p.offset_master)||p.offset_master < -4||p.offset_master > 4 ||
      !std::isfinite(p.gamma_master)||p.gamma_master < .01f||p.gamma_master > 10 ||
      !std::isfinite(p.gain_master)||p.gain_master < 0||p.gain_master > 16)
    throw std::invalid_argument("primary wheels master outside documented range");
  return std::shared_ptr<const PrimaryWheelsParameters>(new PrimaryWheelsParameters(p));
}
bool PrimaryWheelsParameters::is_identity() const noexcept {
  const auto& p=values_;
  return (!p.lift_enabled||(p.lift.r==0&&p.lift.g==0&&p.lift.b==0&&p.lift_master==0))&&
    (!p.gamma_enabled||(p.gamma.r==1&&p.gamma.g==1&&p.gamma.b==1&&p.gamma_master==1))&&
    (!p.gain_enabled||(p.gain.r==1&&p.gain.g==1&&p.gain.b==1&&p.gain_master==1))&&
    (!p.offset_enabled||(p.offset.r==0&&p.offset.g==0&&p.offset.b==0&&p.offset_master==0));
}
Color apply_primary_wheels_reference(Color c,const PrimaryWheelsParameters& parameters) noexcept {
  reference_calls.fetch_add(1,std::memory_order_relaxed); const auto&p=parameters.values();
  const float alpha=c.a;
  c.r=channel(c.r,p.lift.r,p.gamma.r,p.gain.r,p.offset.r,p);
  c.g=channel(c.g,p.lift.g,p.gamma.g,p.gain.g,p.offset.g,p);
  c.b=channel(c.b,p.lift.b,p.gamma.b,p.gain.b,p.offset.b,p); c.a=alpha; return c;
}
void apply_primary_wheels_reference(std::span<const Color> in,std::span<Color> out,const PrimaryWheelsParameters&p){
  if(in.size()!=out.size())throw std::invalid_argument("primary wheels image sizes differ");
  for(std::size_t i=0;i<in.size();++i)out[i]=apply_primary_wheels_reference(in[i],p);
}
std::uint64_t primary_wheels_reference_count()noexcept{return reference_calls.load(std::memory_order_relaxed);}
void reset_primary_wheels_reference_count()noexcept{reference_calls.store(0,std::memory_order_relaxed);}
GraphPass add_primary_wheels_cpu_pass(RenderGraph&g,GraphResource s,GraphResource d,std::shared_ptr<const PrimaryWheelsParameters>p,std::span<const Color>i,std::span<Color>o){
  if(!p)throw std::invalid_argument("null primary wheels parameters");
  RenderPass pass;pass.name="Primary Wheels CPU Reference";pass.reads={{s,ResourceState::shader_read}};pass.writes={{d,ResourceState::shader_write}};pass.execute=[p=std::move(p),i,o](CommandEncoder&){apply_primary_wheels_reference(i,o,*p);};return g.add_pass(std::move(pass));}
GraphPass add_primary_wheels_pass(RenderGraph&g,GraphResource s,GraphResource d,std::function<void(CommandEncoder&)>e){if(!e)throw std::invalid_argument("primary wheels native executor is required");RenderPass p;p.name="Primary Wheels Native";p.reads={{s,ResourceState::shader_read}};p.writes={{d,ResourceState::shader_write}};p.execute=std::move(e);return g.add_pass(std::move(p));}
} // namespace digitor
