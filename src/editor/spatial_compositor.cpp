#include "digitor/spatial_compositor.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace digitor {
namespace {
float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
PixelF transparent() { return {}; }

PixelF sample_nearest(const FrameF& f, double x, double y) {
  const int ix = int(std::llround(x));
  const int iy = int(std::llround(y));
  if (ix < 0 || iy < 0 || ix >= int(f.width) || iy >= int(f.height)) {
    return transparent();
  }
  return f.pixels[std::size_t(iy) * f.width + ix];
}

PixelF lerp(PixelF a, PixelF b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
          a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

PixelF sample_bilinear(const FrameF& f, double x, double y) {
  const int x0 = int(std::floor(x));
  const int y0 = int(std::floor(y));
  const float tx = float(x - x0);
  const float ty = float(y - y0);
  auto at = [&](int px, int py) {
    if (px < 0 || py < 0 || px >= int(f.width) || py >= int(f.height)) {
      return transparent();
    }
    return f.pixels[std::size_t(py) * f.width + px];
  };
  return lerp(lerp(at(x0, y0), at(x0 + 1, y0), tx),
              lerp(at(x0, y0 + 1), at(x0 + 1, y0 + 1), tx), ty);
}

PixelF blend_pixel(PixelF d, PixelF s, BlendMode m, float opacity) {
  s.a = clamp01(s.a * opacity);
  float br = s.r;
  float bg = s.g;
  float bb = s.b;
  if (m == BlendMode::add) {
    br = clamp01(d.r + s.r);
    bg = clamp01(d.g + s.g);
    bb = clamp01(d.b + s.b);
  } else if (m == BlendMode::multiply) {
    br = d.r * s.r;
    bg = d.g * s.g;
    bb = d.b * s.b;
  } else if (m == BlendMode::screen) {
    br = 1 - (1 - d.r) * (1 - s.r);
    bg = 1 - (1 - d.g) * (1 - s.g);
    bb = 1 - (1 - d.b) * (1 - s.b);
  }
  const float oa = s.a + d.a * (1 - s.a);
  if (oa <= 0) {
    return {};
  }
  return {clamp01((br * s.a + d.r * d.a * (1 - s.a)) / oa),
          clamp01((bg * s.a + d.g * d.a * (1 - s.a)) / oa),
          clamp01((bb * s.a + d.b * d.a * (1 - s.a)) / oa), clamp01(oa)};
}

MotionSample motion_at(const std::vector<MotionSample>& motion, std::int64_t frame) {
  if (motion.empty()) {
    return {};
  }
  if (frame <= motion.front().frame) {
    return motion.front();
  }
  if (frame >= motion.back().frame) {
    return motion.back();
  }
  auto hi = std::upper_bound(motion.begin(), motion.end(), frame,
                             [](auto f, const MotionSample& sample) {
                               return f < sample.frame;
                             });
  auto lo = hi - 1;
  const double t = double(frame - lo->frame) / double(hi->frame - lo->frame);
  MotionSample result;
  result.frame = frame;
  result.x = lo->x + (hi->x - lo->x) * t;
  result.y = lo->y + (hi->y - lo->y) * t;
  result.rotation_degrees =
      lo->rotation_degrees + (hi->rotation_degrees - lo->rotation_degrees) * t;
  result.scale = lo->scale + (hi->scale - lo->scale) * t;
  return result;
}
}  // namespace

double evaluate(const AnimatedScalar& animation, std::int64_t frame) {
  if (animation.keys.empty()) {
    return animation.default_value;
  }
  if (frame <= animation.keys.front().frame) {
    return animation.keys.front().value;
  }
  if (frame >= animation.keys.back().frame) {
    return animation.keys.back().value;
  }
  auto hi = std::upper_bound(animation.keys.begin(), animation.keys.end(), frame,
                             [](auto f, const AnimationKey& key) {
                               return f < key.frame;
                             });
  auto lo = hi - 1;
  const double t = double(frame - lo->frame) / double(hi->frame - lo->frame);
  return lo->value + (hi->value - lo->value) * t;
}

TransformState apply_stabilization(const TransformState& base,
                                   const StabilizationState& settings,
                                   const std::vector<MotionSample>& track,
                                   std::int64_t frame) {
  if (!settings.enabled || track.empty()) {
    return base;
  }
  const auto motion = motion_at(track, frame);
  TransformState output = base;
  const double strength = std::clamp(settings.strength, 0.0, 1.0);
  output.position.x -= motion.x * strength;
  output.position.y -= motion.y * strength;
  output.rotation_degrees -= motion.rotation_degrees * strength;
  const double zoom = 1.0 + std::clamp(settings.crop_ratio, 0.0, 0.5);
  output.scale.x *= zoom / std::max(0.01, motion.scale);
  output.scale.y *= zoom / std::max(0.01, motion.scale);
  return output;
}

RenderResult render_spatial(const FrameF& input, std::uint32_t output_width,
                            std::uint32_t output_height,
                            const SpatialSettings& settings,
                            const RenderPolicy& policy) {
  if (!input.valid() || !output_width || !output_height) {
    throw std::invalid_argument("invalid spatial frame");
  }
  if (policy.backend != VisualBackend::cpu && !policy.gpu_available &&
      !policy.allow_cpu_fallback) {
    return {{}, false,
            "requested GPU backend unavailable and CPU fallback disabled"};
  }
  FrameF output{output_width, output_height,
                std::vector<PixelF>(std::size_t(output_width) * output_height)};
  const auto& transform = settings.transform;
  const auto& crop = settings.crop.normalized;
  const double sx = std::max(std::abs(transform.scale.x), 1e-9);
  const double sy = std::max(std::abs(transform.scale.y), 1e-9);
  const double radians =
      transform.rotation_degrees * 3.14159265358979323846 / 180.0;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  for (std::uint32_t y = 0; y < output_height; ++y) {
    for (std::uint32_t x = 0; x < output_width; ++x) {
      double nx = (double(x) + .5) / output_width - transform.position.x -
                  transform.anchor.x;
      double ny = (double(y) + .5) / output_height - transform.position.y -
                  transform.anchor.y;
      double rx = (cosine * nx + sine * ny) / sx;
      double ry = (-sine * nx + cosine * ny) / sy;
      if (transform.flip_x) rx = -rx;
      if (transform.flip_y) ry = -ry;
      const double u = rx + transform.anchor.x;
      const double v = ry + transform.anchor.y;
      if (u < crop.left || u > crop.right || v < crop.top || v > crop.bottom) {
        output.pixels[std::size_t(y) * output_width + x] = {};
        continue;
      }
      const double ix = u * input.width - .5;
      const double iy = v * input.height - .5;
      auto pixel = settings.sampling == SamplingMode::nearest
                       ? sample_nearest(input, ix, iy)
                       : sample_bilinear(input, ix, iy);
      pixel.a = clamp01(float(pixel.a * transform.opacity));
      output.pixels[std::size_t(y) * output_width + x] = pixel;
    }
  }
  return {std::move(output),
          policy.backend != VisualBackend::cpu && policy.gpu_available, {}};
}

void apply_chroma_key(FrameF& frame, const ChromaKeyState& settings) {
  if (!settings.enabled) {
    return;
  }
  const float key_r = float(settings.key_r);
  const float key_g = float(settings.key_g);
  const float key_b = float(settings.key_b);
  const float similarity = float(std::clamp(settings.similarity, 0.0, 1.0));
  const float softness = float(std::max(settings.softness, 1e-6));
  const float spill = float(std::clamp(settings.spill, 0.0, 1.0));
  for (auto& pixel : frame.pixels) {
    const float distance =
        std::sqrt((pixel.r - key_r) * (pixel.r - key_r) +
                  (pixel.g - key_g) * (pixel.g - key_g) +
                  (pixel.b - key_b) * (pixel.b - key_b)) /
        1.7320508f;
    const float matte = clamp01((distance - similarity) / softness);
    pixel.a *= matte;
    const float dominance = std::max(0.0f, pixel.g - std::max(pixel.r, pixel.b));
    pixel.g = clamp01(pixel.g - dominance * spill * (1 - matte));
  }
}

void composite(FrameF& destination, const FrameF& foreground, BlendMode mode,
               double opacity) {
  if (!destination.valid() || !foreground.valid() ||
      destination.width != foreground.width ||
      destination.height != foreground.height) {
    throw std::invalid_argument("incompatible composite frames");
  }
  for (std::size_t i = 0; i < destination.pixels.size(); ++i) {
    destination.pixels[i] = blend_pixel(
        destination.pixels[i], foreground.pixels[i], mode,
        float(std::clamp(opacity, 0.0, 1.0)));
  }
}

RenderResult render_layer(const FrameF& foreground, const FrameF& background,
                          const LayerSettings& settings,
                          const StabilizationState& stabilization,
                          const std::vector<MotionSample>& track,
                          std::int64_t frame, const RenderPolicy& policy) {
  if (!background.valid()) {
    throw std::invalid_argument("invalid background");
  }
  auto spatial_settings = settings.spatial;
  spatial_settings.transform = apply_stabilization(
      spatial_settings.transform, stabilization, track, frame);
  auto result = render_spatial(foreground, background.width, background.height,
                               spatial_settings, policy);
  if (!result.frame.valid()) {
    return result;
  }
  apply_chroma_key(result.frame, settings.chroma_key);
  FrameF output = background;
  composite(output, result.frame, settings.blend, settings.opacity);
  result.frame = std::move(output);
  return result;
}

std::string stable_frame_digest(const FrameF& frame) {
  if (!frame.valid()) {
    return {};
  }
  std::uint64_t hash = 1469598103934665603ull;
  for (const auto& pixel : frame.pixels) {
    for (float value : {pixel.r, pixel.g, pixel.b, pixel.a}) {
      std::uint32_t bits;
      std::memcpy(&bits, &value, sizeof(bits));
      for (int i = 0; i < 4; ++i) {
        hash ^= (bits >> (i * 8)) & 255u;
        hash *= 1099511628211ull;
      }
    }
  }
  std::ostringstream output;
  output << std::hex << std::setw(16) << std::setfill('0') << hash;
  return output.str();
}
}  // namespace digitor
