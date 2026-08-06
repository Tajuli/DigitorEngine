#include "digitor/effects.hpp"
#include "digitor/cpu_parallel_executor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace digitor {
namespace {
Color mix(Color a, Color b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
          a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
Color sample(const std::vector<Color>& p, int x, int y, int w, int h) {
  x = std::clamp(x, 0, w - 1);
  y = std::clamp(y, 0, h - 1);
  return p[std::size_t(y) * static_cast<std::size_t>(w) +
           static_cast<std::size_t>(x)];
}
float random(std::uint64_t x) {
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  return float((x * 2685821657736338717ULL) >> 40) / float(1u << 24);
}

void run(const Color* in, Color* out, int w, int h, EffectSettings s) {
  const std::size_t pixel_count = std::size_t(w) * std::size_t(h);
  std::vector<Color> src(in, in + pixel_count);
  const int radius =
      std::clamp(int(std::ceil(std::abs(s.radius))), 1, 64);
  const std::size_t halo =
      (s.type == EffectType::blur || s.type == EffectType::glow ||
       s.type == EffectType::motion_blur ||
       s.type == EffectType::chromatic_aberration)
          ? static_cast<std::size_t>(radius)
          : (s.type == EffectType::sharpen ? 1u : 0u);

  shared_cpu_executor().parallel_for_tiles(
      static_cast<std::size_t>(w), static_cast<std::size_t>(h), 64, 32,
      halo, [&](const CpuTile2D& tile) {
        for (std::size_t yy = tile.y_begin; yy < tile.y_end; ++yy) {
          const int y = static_cast<int>(yy);
          for (std::size_t xx = tile.x_begin; xx < tile.x_end; ++xx) {
            const int x = static_cast<int>(xx);
            Color base = sample(src, x, y, w, h), v = base;
            if (s.type == EffectType::blur || s.type == EffectType::glow) {
              Color sum{};
              sum.a = 0;
              int count = 0;
              for (int j = -radius; j <= radius; ++j) {
                for (int i = -radius; i <= radius; ++i) {
                  const auto q = sample(src, x + i, y + j, w, h);
                  sum.r += q.r;
                  sum.g += q.g;
                  sum.b += q.b;
                  sum.a += q.a;
                  ++count;
                }
              }
              v = {sum.r / count, sum.g / count, sum.b / count, base.a};
              if (s.type == EffectType::glow) {
                const float bright = std::max({base.r, base.g, base.b});
                v = mix(base, v,
                        std::clamp((bright - .5f) * 2 * s.amount, 0.f, 1.f));
              }
            } else if (s.type == EffectType::sharpen) {
              const auto b = mix(
                  mix(sample(src, x - 1, y, w, h),
                      sample(src, x + 1, y, w, h), .5f),
                  mix(sample(src, x, y - 1, w, h),
                      sample(src, x, y + 1, w, h), .5f),
                  .5f);
              v = {base.r + (base.r - b.r) * s.amount,
                   base.g + (base.g - b.g) * s.amount,
                   base.b + (base.b - b.b) * s.amount, base.a};
            } else if (s.type == EffectType::vignette) {
              const float dx = (x + .5f) / w * 2 - 1;
              const float dy = (y + .5f) / h * 2 - 1;
              const float f =
                  1 - std::clamp((dx * dx + dy * dy) * .5f * s.amount,
                                 0.f, 1.f);
              v = {base.r * f, base.g * f, base.b * f, base.a};
            } else if (s.type == EffectType::noise ||
                       s.type == EffectType::film_grain) {
              const float n =
                  (random(s.seed + std::uint64_t(y) * std::uint64_t(w) +
                          std::uint64_t(x)) -
                   .5f) *
                  s.amount;
              v = {base.r + n, base.g + n, base.b + n, base.a};
            } else if (s.type == EffectType::chromatic_aberration) {
              const auto l = sample(src, x - radius, y, w, h);
              const auto r = sample(src, x + radius, y, w, h);
              v = {l.r, base.g, r.b, base.a};
            } else if (s.type == EffectType::motion_blur) {
              Color sum{};
              sum.a = 0;
              const float dx = std::cos(s.angle), dy = std::sin(s.angle);
              for (int i = -radius; i <= radius; ++i) {
                const auto q = sample(src, x + int(dx * i), y + int(dy * i),
                                      w, h);
                sum.r += q.r;
                sum.g += q.g;
                sum.b += q.b;
              }
              const float n = 1.f / (2 * radius + 1);
              v = {sum.r * n, sum.g * n, sum.b * n, base.a};
            } else if (s.type == EffectType::lens_distortion) {
              const float nx = (x + .5f) / w * 2 - 1;
              const float ny = (y + .5f) / h * 2 - 1;
              const float k = 1 + s.amount * (nx * nx + ny * ny);
              v = sample(src, int((nx * k + 1) * w * .5f),
                         int((ny * k + 1) * h * .5f), w, h);
            }
            out[yy * static_cast<std::size_t>(w) + xx] = v;
          }
        }
      });
}
}  // namespace

void apply_effect_gpu(CommandEncoder& e, const Color* i, Color* o,
                      std::uint32_t w, std::uint32_t h,
                      const EffectSettings& s) {
  if (!i || !o || !w || !h)
    throw std::invalid_argument("invalid effect image");
  e.dispatch([=] { run(i, o, int(w), int(h), s); });
}
}  // namespace digitor

// Filter, plugin, beauty, effect, visual-stack, and native-effect
// implementations are compiled through this established unit so all platform
// manifests receive the same public runtime.
#include "filter.cpp"
#include "plugin.cpp"

#define clamp01 beauty_clamp01
#define mix beauty_mix
#include "beauty.cpp"
#undef mix
#undef clamp01

#include "beauty_plugin.cpp"
#include "effect_system.cpp"
#include "visual_stack.cpp"
#include "native_effects.cpp"
