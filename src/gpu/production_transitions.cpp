#include "digitor/production_transitions.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace digitor {
namespace {

float clamp01(float value) noexcept {
  return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float value) noexcept {
  if (edge1 <= edge0) {
    return value >= edge1 ? 1.0f : 0.0f;
  }
  const float t = clamp01((value - edge0) / (edge1 - edge0));
  return t * t * (3.0f - 2.0f * t);
}

float eased_progress(const TransitionSettings& settings) noexcept {
  const float p = clamp01(settings.progress);
  return settings.ease_in_out ? p * p * (3.0f - 2.0f * p) : p;
}

TransitionPixel mix(const TransitionPixel& a,
                    const TransitionPixel& b,
                    float t) noexcept {
  const float u = 1.0f - t;
  return {
      a.r * u + b.r * t,
      a.g * u + b.g * t,
      a.b * u + b.b * t,
      a.a * u + b.a * t,
  };
}

TransitionPixel sample_clamped(const TransitionFrame& frame,
                               float u,
                               float v) noexcept {
  u = clamp01(u);
  v = clamp01(v);
  const float x = u * static_cast<float>(frame.width - 1u);
  const float y = v * static_cast<float>(frame.height - 1u);
  const auto x0 = static_cast<std::uint32_t>(x);
  const auto y0 = static_cast<std::uint32_t>(y);
  const auto x1 = std::min(x0 + 1u, frame.width - 1u);
  const auto y1 = std::min(y0 + 1u, frame.height - 1u);
  const float tx = x - static_cast<float>(x0);
  const float ty = y - static_cast<float>(y0);
  const auto& p00 = frame.pixels[static_cast<std::size_t>(y0) * frame.width + x0];
  const auto& p10 = frame.pixels[static_cast<std::size_t>(y0) * frame.width + x1];
  const auto& p01 = frame.pixels[static_cast<std::size_t>(y1) * frame.width + x0];
  const auto& p11 = frame.pixels[static_cast<std::size_t>(y1) * frame.width + x1];
  const auto top = mix(p00, p10, tx);
  const auto bottom = mix(p01, p11, tx);
  return mix(top, bottom, ty);
}

float directional_coordinate(float u,
                             float v,
                             TransitionDirection direction) noexcept {
  switch (direction) {
    case TransitionDirection::left:
      return 1.0f - u;
    case TransitionDirection::right:
      return u;
    case TransitionDirection::up:
      return 1.0f - v;
    case TransitionDirection::down:
      return v;
  }
  return u;
}

bool valid_frame(const TransitionFrame& frame) noexcept {
  return frame.width > 0u && frame.height > 0u &&
         frame.pixels.size() == static_cast<std::size_t>(frame.width) * frame.height;
}

bool valid_settings(const TransitionSettings& settings) noexcept {
  return std::isfinite(settings.progress) &&
         std::isfinite(settings.softness) &&
         settings.softness >= 0.0f && settings.softness <= 1.0f &&
         settings.dip_r >= 0.0f && settings.dip_r <= 1.0f &&
         settings.dip_g >= 0.0f && settings.dip_g <= 1.0f &&
         settings.dip_b >= 0.0f && settings.dip_b <= 1.0f &&
         settings.dip_a >= 0.0f && settings.dip_a <= 1.0f;
}

std::uint64_t append_digest(std::uint64_t hash,
                            const void* data,
                            std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash;
}

}  // namespace

std::uint64_t transition_frame_digest(const TransitionFrame& frame) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append_digest(hash, &frame.width, sizeof(frame.width));
  hash = append_digest(hash, &frame.height, sizeof(frame.height));
  if (!frame.pixels.empty()) {
    hash = append_digest(hash,
                         frame.pixels.data(),
                         frame.pixels.size() * sizeof(TransitionPixel));
  }
  return hash;
}

TransitionResult apply_transition_reference(const TransitionFrame& a,
                                            const TransitionFrame& b,
                                            TransitionFrame& output,
                                            const TransitionSettings& settings) {
  TransitionResult result;
  if (!valid_frame(a) || !valid_frame(b) ||
      a.width != b.width || a.height != b.height ||
      !valid_settings(settings)) {
    return result;
  }

  output.width = a.width;
  output.height = a.height;
  output.pixels.resize(a.pixels.size());
  const float p = eased_progress(settings);
  const TransitionPixel dip{settings.dip_r,
                            settings.dip_g,
                            settings.dip_b,
                            settings.dip_a};

  for (std::uint32_t y = 0; y < a.height; ++y) {
    for (std::uint32_t x = 0; x < a.width; ++x) {
      const std::size_t index = static_cast<std::size_t>(y) * a.width + x;
      const float u = a.width > 1u
                          ? static_cast<float>(x) / static_cast<float>(a.width - 1u)
                          : 0.0f;
      const float v = a.height > 1u
                          ? static_cast<float>(y) / static_cast<float>(a.height - 1u)
                          : 0.0f;

      switch (settings.type) {
        case TransitionType::cross_dissolve:
          output.pixels[index] = mix(a.pixels[index], b.pixels[index], p);
          break;
        case TransitionType::dip_to_color:
          output.pixels[index] = p < 0.5f
                                     ? mix(a.pixels[index], dip, p * 2.0f)
                                     : mix(dip, b.pixels[index], (p - 0.5f) * 2.0f);
          break;
        case TransitionType::wipe: {
          const float coordinate = directional_coordinate(u, v, settings.direction);
          const float blend = smoothstep(p - settings.softness,
                                         p + settings.softness,
                                         coordinate);
          output.pixels[index] = mix(b.pixels[index], a.pixels[index], blend);
          break;
        }
        case TransitionType::slide: {
          float a_u = u;
          float a_v = v;
          float b_u = u;
          float b_v = v;
          switch (settings.direction) {
            case TransitionDirection::left:
              a_u = u + p;
              b_u = u - (1.0f - p);
              break;
            case TransitionDirection::right:
              a_u = u - p;
              b_u = u + (1.0f - p);
              break;
            case TransitionDirection::up:
              a_v = v + p;
              b_v = v - (1.0f - p);
              break;
            case TransitionDirection::down:
              a_v = v - p;
              b_v = v + (1.0f - p);
              break;
          }
          const bool use_b = b_u >= 0.0f && b_u <= 1.0f &&
                             b_v >= 0.0f && b_v <= 1.0f;
          output.pixels[index] = use_b ? sample_clamped(b, b_u, b_v)
                                       : sample_clamped(a, a_u, a_v);
          break;
        }
      }
    }
  }

  result.status = TransitionStatus::ready;
  result.digest = transition_frame_digest(output);
  return result;
}

TransitionResult dispatch_transition_gpu(const TransitionDispatchPacket& packet,
                                         const TransitionDispatch& dispatch) {
  TransitionResult result;
  if (packet.backend == TransitionBackend::cpu ||
      packet.width == 0u || packet.height == 0u ||
      packet.input_a_handle == 0u || packet.input_b_handle == 0u ||
      packet.output_handle == 0u || packet.command_handle == 0u ||
      !valid_settings(packet.settings)) {
    return result;
  }
  if (!dispatch) {
    result.status = TransitionStatus::backend_unavailable;
    return result;
  }
  result.status = dispatch(packet) ? TransitionStatus::ready
                                   : TransitionStatus::dispatch_failed;
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_transition_rgba32f(
    const float* input_a,
    const float* input_b,
    float* output,
    std::uint32_t width,
    std::uint32_t height,
    const DigitorTransitionSettings* settings,
    std::uint64_t* digest) {
  if (!input_a || !input_b || !output || !settings || !digest ||
      width == 0u || height == 0u) {
    return 1u;
  }

  digitor::TransitionFrame a{width, height, {}};
  digitor::TransitionFrame b{width, height, {}};
  const std::size_t count = static_cast<std::size_t>(width) * height;
  a.pixels.resize(count);
  b.pixels.resize(count);
  for (std::size_t index = 0; index < count; ++index) {
    a.pixels[index] = {input_a[index * 4u],
                       input_a[index * 4u + 1u],
                       input_a[index * 4u + 2u],
                       input_a[index * 4u + 3u]};
    b.pixels[index] = {input_b[index * 4u],
                       input_b[index * 4u + 1u],
                       input_b[index * 4u + 2u],
                       input_b[index * 4u + 3u]};
  }

  digitor::TransitionSettings native;
  native.type = static_cast<digitor::TransitionType>(settings->type);
  native.direction = static_cast<digitor::TransitionDirection>(settings->direction);
  native.progress = settings->progress;
  native.softness = settings->softness;
  native.dip_r = settings->dip_r;
  native.dip_g = settings->dip_g;
  native.dip_b = settings->dip_b;
  native.dip_a = settings->dip_a;
  native.ease_in_out = settings->ease_in_out != 0u;

  digitor::TransitionFrame rendered;
  const auto result = digitor::apply_transition_reference(a, b, rendered, native);
  if (result.status != digitor::TransitionStatus::ready) {
    return 2u;
  }

  for (std::size_t index = 0; index < count; ++index) {
    output[index * 4u] = rendered.pixels[index].r;
    output[index * 4u + 1u] = rendered.pixels[index].g;
    output[index * 4u + 2u] = rendered.pixels[index].b;
    output[index * 4u + 3u] = rendered.pixels[index].a;
  }
  *digest = result.digest;
  return 0u;
}
