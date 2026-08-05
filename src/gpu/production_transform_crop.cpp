#include "digitor/production_transform_crop.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace digitor {
namespace {

float clamp01(float value) noexcept {
  return std::clamp(value, 0.0f, 1.0f);
}

bool valid_frame(const TransformCropFrame& frame) noexcept {
  return frame.width != 0u && frame.height != 0u &&
         frame.pixels.size() ==
             static_cast<std::size_t>(frame.width) * frame.height;
}

float mirror_coordinate(float value) noexcept {
  value = std::fmod(std::fabs(value), 2.0f);
  return value <= 1.0f ? value : 2.0f - value;
}

TransformCropPixel mix_pixels(const TransformCropPixel& a,
                              const TransformCropPixel& b,
                              float amount) noexcept {
  return {
      a.r + (b.r - a.r) * amount,
      a.g + (b.g - a.g) * amount,
      a.b + (b.b - a.b) * amount,
      a.a + (b.a - a.a) * amount,
  };
}

TransformCropPixel sample_frame(const TransformCropFrame& frame,
                                float u,
                                float v,
                                TransformCropFilter filter,
                                TransformCropEdge edge) noexcept {
  if (edge == TransformCropEdge::transparent &&
      (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)) {
    return {0.0f, 0.0f, 0.0f, 0.0f};
  }

  if (edge == TransformCropEdge::mirror) {
    u = mirror_coordinate(u);
    v = mirror_coordinate(v);
  } else {
    u = clamp01(u);
    v = clamp01(v);
  }

  const float x = u * static_cast<float>(frame.width - 1u);
  const float y = v * static_cast<float>(frame.height - 1u);
  if (filter == TransformCropFilter::nearest) {
    const auto xi = static_cast<std::uint32_t>(std::lround(x));
    const auto yi = static_cast<std::uint32_t>(std::lround(y));
    return frame.pixels[static_cast<std::size_t>(yi) * frame.width + xi];
  }

  const auto x0 = static_cast<std::uint32_t>(x);
  const auto y0 = static_cast<std::uint32_t>(y);
  const auto x1 = std::min(x0 + 1u, frame.width - 1u);
  const auto y1 = std::min(y0 + 1u, frame.height - 1u);
  const float tx = x - static_cast<float>(x0);
  const float ty = y - static_cast<float>(y0);

  const auto top = mix_pixels(
      frame.pixels[static_cast<std::size_t>(y0) * frame.width + x0],
      frame.pixels[static_cast<std::size_t>(y0) * frame.width + x1], tx);
  const auto bottom = mix_pixels(
      frame.pixels[static_cast<std::size_t>(y1) * frame.width + x0],
      frame.pixels[static_cast<std::size_t>(y1) * frame.width + x1], tx);
  return mix_pixels(top, bottom, ty);
}

bool valid_settings(const TransformCropSettings& settings) noexcept {
  for (const float value : settings.matrix) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return settings.output_width != 0u && settings.output_height != 0u &&
         settings.crop_left >= 0.0f && settings.crop_top >= 0.0f &&
         settings.crop_right <= 1.0f && settings.crop_bottom <= 1.0f &&
         settings.crop_left < settings.crop_right &&
         settings.crop_top < settings.crop_bottom;
}

std::uint64_t append_digest(std::uint64_t hash,
                            const void* data,
                            std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  while (size-- != 0u) {
    hash ^= *bytes++;
    hash *= 1099511628211ull;
  }
  return hash;
}

}  // namespace

std::uint64_t transform_crop_digest(
    const TransformCropFrame& frame) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append_digest(hash, &frame.width, sizeof(frame.width));
  hash = append_digest(hash, &frame.height, sizeof(frame.height));
  if (!frame.pixels.empty()) {
    hash = append_digest(hash, frame.pixels.data(),
                         frame.pixels.size() * sizeof(TransformCropPixel));
  }
  return hash;
}

TransformCropResult apply_transform_crop_reference(
    const TransformCropFrame& input,
    TransformCropFrame& output,
    const TransformCropSettings& settings) {
  TransformCropResult result;
  if (!valid_frame(input) || !valid_settings(settings)) {
    return result;
  }

  output.width = settings.output_width;
  output.height = settings.output_height;
  output.pixels.resize(static_cast<std::size_t>(output.width) * output.height);

  for (std::uint32_t y = 0; y < output.height; ++y) {
    for (std::uint32_t x = 0; x < output.width; ++x) {
      const float output_u = output.width > 1u
                                 ? static_cast<float>(x) /
                                       static_cast<float>(output.width - 1u)
                                 : 0.0f;
      const float output_v = output.height > 1u
                                 ? static_cast<float>(y) /
                                       static_cast<float>(output.height - 1u)
                                 : 0.0f;
      const float crop_u = settings.crop_left +
                           output_u *
                               (settings.crop_right - settings.crop_left);
      const float crop_v = settings.crop_top +
                           output_v *
                               (settings.crop_bottom - settings.crop_top);
      const float px = settings.matrix[0] * crop_u +
                       settings.matrix[1] * crop_v + settings.matrix[2];
      const float py = settings.matrix[3] * crop_u +
                       settings.matrix[4] * crop_v + settings.matrix[5];
      const float pw = settings.matrix[6] * crop_u +
                       settings.matrix[7] * crop_v + settings.matrix[8];

      output.pixels[static_cast<std::size_t>(y) * output.width + x] =
          std::fabs(pw) < 1e-8f
              ? TransformCropPixel{0.0f, 0.0f, 0.0f, 0.0f}
              : sample_frame(input, px / pw, py / pw, settings.filter,
                             settings.edge);
    }
  }

  result.status = TransformCropStatus::ready;
  result.digest = transform_crop_digest(output);
  return result;
}

TransformCropResult dispatch_transform_crop_gpu(
    const TransformCropDispatchPacket& packet,
    const TransformCropDispatch& dispatch) {
  TransformCropResult result;
  if (packet.backend == TransformCropBackend::cpu ||
      packet.input_handle == 0u || packet.output_handle == 0u ||
      packet.command_handle == 0u || packet.input_width == 0u ||
      packet.input_height == 0u || !valid_settings(packet.settings)) {
    return result;
  }
  if (!dispatch) {
    result.status = TransformCropStatus::backend_unavailable;
    return result;
  }
  result.status = dispatch(packet) ? TransformCropStatus::ready
                                   : TransformCropStatus::dispatch_failed;
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_transform_crop_rgba32f(
    const float* input,
    std::uint32_t input_width,
    std::uint32_t input_height,
    float* output,
    const DigitorTransformCropSettings* settings,
    std::uint64_t* digest) {
  if (!input || !output || !settings || !digest || input_width == 0u ||
      input_height == 0u) {
    return 1u;
  }

  digitor::TransformCropFrame input_frame{input_width, input_height, {}};
  input_frame.pixels.resize(static_cast<std::size_t>(input_width) *
                            input_height);
  for (std::size_t index = 0; index < input_frame.pixels.size(); ++index) {
    input_frame.pixels[index] = {
        input[index * 4u], input[index * 4u + 1u],
        input[index * 4u + 2u], input[index * 4u + 3u]};
  }

  digitor::TransformCropSettings native_settings;
  for (std::size_t index = 0; index < 9u; ++index) {
    native_settings.matrix[index] = settings->matrix[index];
  }
  native_settings.crop_left = settings->crop_left;
  native_settings.crop_top = settings->crop_top;
  native_settings.crop_right = settings->crop_right;
  native_settings.crop_bottom = settings->crop_bottom;
  native_settings.output_width = settings->output_width;
  native_settings.output_height = settings->output_height;
  native_settings.filter =
      static_cast<digitor::TransformCropFilter>(settings->filter);
  native_settings.edge =
      static_cast<digitor::TransformCropEdge>(settings->edge);

  digitor::TransformCropFrame rendered;
  const auto result = digitor::apply_transform_crop_reference(
      input_frame, rendered, native_settings);
  if (result.status != digitor::TransformCropStatus::ready) {
    return 2u;
  }

  for (std::size_t index = 0; index < rendered.pixels.size(); ++index) {
    output[index * 4u] = rendered.pixels[index].r;
    output[index * 4u + 1u] = rendered.pixels[index].g;
    output[index * 4u + 2u] = rendered.pixels[index].b;
    output[index * 4u + 3u] = rendered.pixels[index].a;
  }
  *digest = result.digest;
  return 0u;
}
