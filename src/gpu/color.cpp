#include "digitor/color.hpp"
#include "core/engine.hpp"
#include "gpu/execution_provenance.hpp"
#include <algorithm>
#include <cmath>
namespace digitor {
namespace {
float lin(float x) {
  return x <= .04045f ? x / 12.92f : std::pow((x + .055f) / 1.055f, 2.4f);
}
float enc(float x) {
  return x <= .0031308f ? 12.92f * x : 1.055f * std::pow(x, 1 / 2.4f) - .055f;
}
} // namespace
Color linearize_srgb(Color c) { return {lin(c.r), lin(c.g), lin(c.b), c.a}; }
Color encode_srgb(Color c) { return {enc(c.r), enc(c.g), enc(c.b), c.a}; }
Color grade_color(Color c, const ColorGrade &p) {
  float x[3]{c.r, c.g, c.b};
  float temp = p.temperature * .1f, tint = p.tint * .1f;
  x[0] += temp;
  x[2] -= temp;
  x[1] += tint;
  float lum = .2126f * x[0] + .7152f * x[1] + .0722f * x[2];
  float mx = std::max({x[0], x[1], x[2]}), mn = std::min({x[0], x[1], x[2]});
  float vib = 1 + p.vibrance * (1 - (mx - mn));
  float sat = p.saturation * vib;
  for (float &v : x) {
    v = lum + (v - lum) * sat;
    v = (v - .5f) * p.contrast + .5f;
    v = (v + p.lift) * p.gain + p.offset;
    v *= std::exp2(p.exposure);
    v = std::copysign(std::pow(std::abs(v), 1 / std::max(.001f, p.gamma)), v);
  }
  float a = p.hue * 3.1415926535f / 180, co = std::cos(a), s = std::sin(a);
  float r = x[0], g = x[1], b = x[2];
  x[0] = (.213f + co * .787f - s * .213f) * r +
         (.715f - co * .715f - s * .715f) * g +
         (.072f - co * .072f + s * .928f) * b;
  x[1] = (.213f - co * .213f + s * .143f) * r +
         (.715f + co * .285f + s * .140f) * g +
         (.072f - co * .072f - s * .283f) * b;
  x[2] = (.213f - co * .213f - s * .787f) * r +
         (.715f - co * .715f + s * .715f) * g +
         (.072f + co * .928f + s * .072f) * b;
  return {x[0], x[1], x[2], c.a};
}
void grade_image_cpu(const Color *i, Color *o, size_t n, const ColorGrade &p) {
  note_cpu_color_reference();
  for (size_t k = 0; k < n; ++k)
    o[k] = grade_color(i[k], p);
}
void grade_image_gpu(CommandEncoder &e, const Color *i, Color *o, size_t n,
                     const ColorGrade &p) {
  if (!i || !o)
    throw std::invalid_argument("null color image");
  e.dispatch([=] {
    if (!Engine::instance().is_initialized())
      throw std::runtime_error("native GPU color dispatch requires an initialized GPU backend");
    if (Engine::instance().grade_rgba32f({i, n}, {o, n}, p) !=
        DIGITOR_RESULT_OK)
      throw std::runtime_error("native GPU color dispatch failed");
  });
}
} // namespace digitor
