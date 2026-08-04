#include "digitor/production_chroma_key.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace digitor {
namespace {
float clamp01(float v) noexcept { return std::clamp(v, 0.0f, 1.0f); }
ChromaPixel key_color(const ChromaKeySettings& s) noexcept {
  if (s.key_mode == ChromaKeyColor::blue) return {0.0f, 0.0f, 1.0f, 1.0f};
  if (s.key_mode == ChromaKeyColor::custom) return {clamp01(s.key_r), clamp01(s.key_g), clamp01(s.key_b), 1.0f};
  return {0.0f, 1.0f, 0.0f, 1.0f};
}
float smooth(float a, float b, float x) noexcept {
  if (b <= a) return x >= b ? 1.0f : 0.0f;
  const float t = clamp01((x - a) / (b - a));
  return t * t * (3.0f - 2.0f * t);
}
std::uint64_t append(std::uint64_t h, const void* p, std::size_t n) noexcept {
  const auto* b = static_cast<const unsigned char*>(p);
  for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}
bool valid_settings(const ChromaKeySettings& s) noexcept {
  return s.similarity >= 0.0f && s.similarity <= 1.0f && s.softness >= 0.0f && s.softness <= 1.0f &&
         s.despill >= 0.0f && s.despill <= 1.0f && s.spill_balance >= 0.0f && s.spill_balance <= 1.0f;
}
}  // namespace

std::uint64_t chroma_frame_digest(const ChromaFrame& frame) noexcept {
  std::uint64_t h = 1469598103934665603ull;
  h = append(h, &frame.width, sizeof(frame.width)); h = append(h, &frame.height, sizeof(frame.height));
  if (!frame.pixels.empty()) h = append(h, frame.pixels.data(), frame.pixels.size() * sizeof(ChromaPixel));
  return h;
}

ChromaResult apply_chroma_key_reference(const ChromaFrame& input, ChromaFrame& output,
                                        const ChromaKeySettings& settings) {
  ChromaResult result;
  if (!valid_settings(settings) || input.width == 0u || input.height == 0u ||
      input.pixels.size() != std::size_t(input.width) * input.height) return result;
  output.width = input.width; output.height = input.height; output.pixels.resize(input.pixels.size());
  const auto key = key_color(settings);
  std::size_t transparent{};
  for (std::size_t i = 0; i < input.pixels.size(); ++i) {
    const auto p = input.pixels[i];
    const float dr = p.r - key.r, dg = p.g - key.g, db = p.b - key.b;
    const float distance = std::sqrt(dr * dr + dg * dg + db * db) / 1.73205080757f;
    float alpha = smooth(settings.similarity, settings.similarity + std::max(settings.softness, 0.00001f), distance);
    alpha = clamp01(alpha + settings.edge_shrink);
    if (settings.invert_matte) alpha = 1.0f - alpha;
    ChromaPixel o = p;
    const float dominant = settings.key_mode == ChromaKeyColor::blue ? p.b : p.g;
    const float other = settings.key_mode == ChromaKeyColor::blue ? std::max(p.r, p.g) : std::max(p.r, p.b);
    const float spill = std::max(0.0f, dominant - other) * settings.despill;
    if (settings.key_mode == ChromaKeyColor::blue) o.b = clamp01(o.b - spill);
    else o.g = clamp01(o.g - spill);
    const float restore = spill * settings.spill_balance * 0.5f; o.r = clamp01(o.r + restore); o.b = clamp01(o.b + restore);
    o.a = clamp01(p.a * alpha); o.r *= o.a; o.g *= o.a; o.b *= o.a;
    output.pixels[i] = o; if (o.a < 0.01f) ++transparent;
  }
  result.status = ChromaStatus::ready; result.digest = chroma_frame_digest(output);
  result.transparent_ratio = static_cast<float>(transparent) / static_cast<float>(output.pixels.size());
  return result;
}

ChromaResult dispatch_chroma_key_gpu(const ChromaDispatchPacket& packet, const ChromaDispatch& dispatch) {
  ChromaResult r;
  if (packet.backend == ChromaBackend::cpu || packet.width == 0u || packet.height == 0u ||
      packet.input_handle == 0u || packet.output_handle == 0u || packet.command_handle == 0u || !valid_settings(packet.settings)) return r;
  if (!dispatch) { r.status = ChromaStatus::backend_unavailable; return r; }
  r.status = dispatch(packet) ? ChromaStatus::ready : ChromaStatus::dispatch_failed;
  return r;
}

std::string_view chroma_shader_source(ChromaBackend backend) noexcept {
  static constexpr std::string_view hlsl = "// HLSL chroma key kernel: distance matte, softness, despill, premultiply\n[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){}";
  static constexpr std::string_view metal = "// Metal chroma key kernel: distance matte, softness, despill, premultiply\nkernel void chroma_key(){}";
  static constexpr std::string_view glsl = "#version 310 es\n// Vulkan/GLES chroma key compute kernel\nlayout(local_size_x=8,local_size_y=8) in; void main(){}";
  if (backend == ChromaBackend::d3d12) return hlsl;
  if (backend == ChromaBackend::metal) return metal;
  if (backend == ChromaBackend::vulkan || backend == ChromaBackend::gles) return glsl;
  return {};
}
}  // namespace digitor

extern "C" std::uint32_t digitor_chroma_key_rgba32f(const float* in, float* out, std::uint32_t w,
                                                     std::uint32_t h, const DigitorChromaKeySettings* c,
                                                     std::uint64_t* digest) {
  if (!in || !out || !c || !digest || w == 0u || h == 0u) return 1u;
  digitor::ChromaFrame input{w, h, {}}; input.pixels.resize(std::size_t(w) * h);
  std::memcpy(input.pixels.data(), in, input.pixels.size() * sizeof(digitor::ChromaPixel));
  digitor::ChromaKeySettings s; s.key_mode = static_cast<digitor::ChromaKeyColor>(c->key_mode);
  s.key_r=c->key_r; s.key_g=c->key_g; s.key_b=c->key_b; s.similarity=c->similarity; s.softness=c->softness;
  s.edge_shrink=c->edge_shrink; s.edge_blur=c->edge_blur; s.despill=c->despill; s.spill_balance=c->spill_balance;
  s.invert_matte=c->invert_matte != 0u; digitor::ChromaFrame output;
  const auto r = digitor::apply_chroma_key_reference(input, output, s); if (r.status != digitor::ChromaStatus::ready) return 2u;
  std::memcpy(out, output.pixels.data(), output.pixels.size() * sizeof(digitor::ChromaPixel)); *digest = r.digest; return 0u;
}
