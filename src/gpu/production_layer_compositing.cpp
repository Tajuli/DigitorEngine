#include "digitor/production_layer_compositing.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace digitor {
namespace {

float clamp01(float value) noexcept {
  return std::clamp(value, 0.0f, 1.0f);
}

bool valid_frame(const CompositeFrame& frame) noexcept {
  return frame.width > 0u && frame.height > 0u &&
         frame.pixels.size() ==
             static_cast<std::size_t>(frame.width) * frame.height;
}

bool valid_settings(const CompositeSettings& settings) noexcept {
  return std::isfinite(settings.opacity) && settings.opacity >= 0.0f &&
         settings.opacity <= 1.0f;
}

float blend_channel(float background, float foreground, BlendMode mode) noexcept {
  switch (mode) {
    case BlendMode::normal:
      return foreground;
    case BlendMode::multiply:
      return background * foreground;
    case BlendMode::screen:
      return 1.0f - (1.0f - background) * (1.0f - foreground);
    case BlendMode::overlay:
      return background <= 0.5f
                 ? 2.0f * background * foreground
                 : 1.0f - 2.0f * (1.0f - background) * (1.0f - foreground);
    case BlendMode::darken:
      return std::min(background, foreground);
    case BlendMode::lighten:
      return std::max(background, foreground);
    case BlendMode::add:
      return clamp01(background + foreground);
    case BlendMode::subtract:
      return clamp01(background - foreground);
    case BlendMode::difference:
      return std::fabs(background - foreground);
  }
  return foreground;
}

CompositePixel to_straight(CompositePixel pixel,
                           CompositeAlphaMode mode) noexcept {
  if (mode == CompositeAlphaMode::premultiplied && pixel.a > 0.0f) {
    pixel.r /= pixel.a;
    pixel.g /= pixel.a;
    pixel.b /= pixel.a;
  }
  pixel.r = clamp01(pixel.r);
  pixel.g = clamp01(pixel.g);
  pixel.b = clamp01(pixel.b);
  pixel.a = clamp01(pixel.a);
  return pixel;
}

CompositePixel compose_pixel(const CompositePixel& background_input,
                             const CompositePixel& foreground_input,
                             const CompositeSettings& settings) noexcept {
  const CompositePixel background = to_straight(background_input, settings.input_alpha);
  CompositePixel foreground = to_straight(foreground_input, settings.input_alpha);
  foreground.a *= settings.opacity;

  const float blended_r = blend_channel(background.r, foreground.r,
                                        settings.blend_mode);
  const float blended_g = blend_channel(background.g, foreground.g,
                                        settings.blend_mode);
  const float blended_b = blend_channel(background.b, foreground.b,
                                        settings.blend_mode);
  const float output_alpha = foreground.a + background.a * (1.0f - foreground.a);

  CompositePixel output{};
  output.a = output_alpha;
  if (output_alpha > 0.0f) {
    output.r = (blended_r * foreground.a +
                background.r * background.a * (1.0f - foreground.a)) /
               output_alpha;
    output.g = (blended_g * foreground.a +
                background.g * background.a * (1.0f - foreground.a)) /
               output_alpha;
    output.b = (blended_b * foreground.a +
                background.b * background.a * (1.0f - foreground.a)) /
               output_alpha;
  }
  output.r = clamp01(output.r);
  output.g = clamp01(output.g);
  output.b = clamp01(output.b);
  output.a = clamp01(output.a);

  if (settings.output_alpha == CompositeAlphaMode::premultiplied) {
    output.r *= output.a;
    output.g *= output.a;
    output.b *= output.a;
  }
  return output;
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

std::uint64_t composite_frame_digest(const CompositeFrame& frame) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append_digest(hash, &frame.width, sizeof(frame.width));
  hash = append_digest(hash, &frame.height, sizeof(frame.height));
  if (!frame.pixels.empty()) {
    hash = append_digest(hash, frame.pixels.data(),
                         frame.pixels.size() * sizeof(CompositePixel));
  }
  return hash;
}

CompositeResult composite_reference(const CompositeFrame& background,
                                    const CompositeFrame& foreground,
                                    CompositeFrame& output,
                                    const CompositeSettings& settings) {
  CompositeResult result;
  if (!valid_frame(background) || !valid_frame(foreground) ||
      background.width != foreground.width ||
      background.height != foreground.height || !valid_settings(settings)) {
    return result;
  }

  output.width = background.width;
  output.height = background.height;
  output.pixels.resize(background.pixels.size());
  for (std::size_t index = 0; index < output.pixels.size(); ++index) {
    output.pixels[index] =
        compose_pixel(background.pixels[index], foreground.pixels[index], settings);
  }
  result.status = CompositeStatus::ready;
  result.digest = composite_frame_digest(output);
  return result;
}

CompositeResult dispatch_composite_gpu(const CompositeDispatchPacket& packet,
                                       const CompositeDispatch& dispatch) {
  CompositeResult result;
  if (packet.backend == CompositeBackend::cpu || packet.width == 0u ||
      packet.height == 0u || packet.background_handle == 0u ||
      packet.foreground_handle == 0u || packet.output_handle == 0u ||
      packet.command_handle == 0u || !valid_settings(packet.settings)) {
    return result;
  }
  if (!dispatch) {
    result.status = CompositeStatus::backend_unavailable;
    return result;
  }
  result.status = dispatch(packet) ? CompositeStatus::ready
                                   : CompositeStatus::dispatch_failed;
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_composite_rgba32f(
    const float* background,
    const float* foreground,
    float* output,
    std::uint32_t width,
    std::uint32_t height,
    const DigitorCompositeSettings* settings,
    std::uint64_t* digest) {
  if (!background || !foreground || !output || !settings || !digest ||
      width == 0u || height == 0u) {
    return 1u;
  }

  const std::size_t count = static_cast<std::size_t>(width) * height;
  digitor::CompositeFrame background_frame{width, height, {}};
  digitor::CompositeFrame foreground_frame{width, height, {}};
  background_frame.pixels.resize(count);
  foreground_frame.pixels.resize(count);
  for (std::size_t index = 0; index < count; ++index) {
    background_frame.pixels[index] = {
        background[index * 4u], background[index * 4u + 1u],
        background[index * 4u + 2u], background[index * 4u + 3u]};
    foreground_frame.pixels[index] = {
        foreground[index * 4u], foreground[index * 4u + 1u],
        foreground[index * 4u + 2u], foreground[index * 4u + 3u]};
  }

  digitor::CompositeSettings native;
  native.blend_mode = static_cast<digitor::BlendMode>(settings->blend_mode);
  native.input_alpha =
      static_cast<digitor::CompositeAlphaMode>(settings->input_alpha);
  native.output_alpha =
      static_cast<digitor::CompositeAlphaMode>(settings->output_alpha);
  native.opacity = settings->opacity;

  digitor::CompositeFrame rendered;
  const auto result = digitor::composite_reference(
      background_frame, foreground_frame, rendered, native);
  if (result.status != digitor::CompositeStatus::ready) {
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
