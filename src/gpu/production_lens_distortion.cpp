#include "digitor/production_lens_distortion.hpp"

#include <algorithm>
#include <cmath>

namespace digitor {
namespace {

float clamp01(float value) noexcept {
  return std::clamp(value, 0.0f, 1.0f);
}

float mirror_coordinate(float value) noexcept {
  value = std::fmod(std::fabs(value), 2.0f);
  return value > 1.0f ? 2.0f - value : value;
}

bool valid_settings(const LensDistortionSettings& settings) noexcept {
  return std::isfinite(settings.k1) && std::isfinite(settings.k2) &&
         std::isfinite(settings.k3) && std::isfinite(settings.p1) &&
         std::isfinite(settings.p2) && settings.scale > 0.0f &&
         settings.aspect > 0.0f && settings.center_x >= 0.0f &&
         settings.center_x <= 1.0f && settings.center_y >= 0.0f &&
         settings.center_y <= 1.0f && settings.chromatic_aberration >= 0.0f &&
         settings.chromatic_aberration <= 0.1f;
}

std::uint64_t append_hash(std::uint64_t hash, const void* data,
                          std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash;
}

LensPixel sample(const LensFrame& frame, float u, float v,
                 LensEdgeMode edge_mode) noexcept {
  if (edge_mode == LensEdgeMode::mirror) {
    u = mirror_coordinate(u);
    v = mirror_coordinate(v);
  } else if (edge_mode == LensEdgeMode::clamp) {
    u = clamp01(u);
    v = clamp01(v);
  } else if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
    return {};
  }

  const float x = u * static_cast<float>(frame.width - 1u);
  const float y = v * static_cast<float>(frame.height - 1u);
  const auto x0 = static_cast<std::uint32_t>(x);
  const auto y0 = static_cast<std::uint32_t>(y);
  const auto x1 = std::min(x0 + 1u, frame.width - 1u);
  const auto y1 = std::min(y0 + 1u, frame.height - 1u);
  const float tx = x - static_cast<float>(x0);
  const float ty = y - static_cast<float>(y0);

  const auto p00 = frame.pixels[std::size_t(y0) * frame.width + x0];
  const auto p10 = frame.pixels[std::size_t(y0) * frame.width + x1];
  const auto p01 = frame.pixels[std::size_t(y1) * frame.width + x0];
  const auto p11 = frame.pixels[std::size_t(y1) * frame.width + x1];

  LensPixel output;
  float* destination = &output.r;
  const float* top_left = &p00.r;
  const float* top_right = &p10.r;
  const float* bottom_left = &p01.r;
  const float* bottom_right = &p11.r;
  for (int channel = 0; channel < 4; ++channel) {
    const float top = top_left[channel] * (1.0f - tx) + top_right[channel] * tx;
    const float bottom =
        bottom_left[channel] * (1.0f - tx) + bottom_right[channel] * tx;
    destination[channel] = top * (1.0f - ty) + bottom * ty;
  }
  return output;
}

void map_coordinate(float u, float v, const LensDistortionSettings& settings,
                    float& source_u, float& source_v) noexcept {
  float x = (u - settings.center_x) * 2.0f * settings.aspect / settings.scale;
  float y = (v - settings.center_y) * 2.0f / settings.scale;

  if (settings.mode == LensMode::correct) {
    const float distorted_x = x;
    const float distorted_y = y;
    for (int iteration = 0; iteration < 5; ++iteration) {
      const float radius_squared = x * x + y * y;
      const float radial =
          1.0f + settings.k1 * radius_squared +
          settings.k2 * radius_squared * radius_squared +
          settings.k3 * radius_squared * radius_squared * radius_squared;
      const float delta_x = 2.0f * settings.p1 * x * y +
                            settings.p2 * (radius_squared + 2.0f * x * x);
      const float delta_y = settings.p1 * (radius_squared + 2.0f * y * y) +
                            2.0f * settings.p2 * x * y;
      if (std::fabs(radial) < 0.000001f) {
        break;
      }
      x = (distorted_x - delta_x) / radial;
      y = (distorted_y - delta_y) / radial;
    }
  } else {
    const float radius_squared = x * x + y * y;
    const float radial =
        1.0f + settings.k1 * radius_squared +
        settings.k2 * radius_squared * radius_squared +
        settings.k3 * radius_squared * radius_squared * radius_squared;
    const float delta_x = 2.0f * settings.p1 * x * y +
                          settings.p2 * (radius_squared + 2.0f * x * x);
    const float delta_y = settings.p1 * (radius_squared + 2.0f * y * y) +
                          2.0f * settings.p2 * x * y;
    x = x * radial + delta_x;
    y = y * radial + delta_y;
  }

  source_u = settings.center_x + x * settings.scale / (2.0f * settings.aspect);
  source_v = settings.center_y + y * settings.scale / 2.0f;
}

}  // namespace

std::uint64_t lens_frame_digest(const LensFrame& frame) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append_hash(hash, &frame.width, sizeof(frame.width));
  hash = append_hash(hash, &frame.height, sizeof(frame.height));
  if (!frame.pixels.empty()) {
    hash = append_hash(hash, frame.pixels.data(),
                       frame.pixels.size() * sizeof(LensPixel));
  }
  return hash;
}

LensResult apply_lens_distortion_reference(
    const LensFrame& input, LensFrame& output,
    const LensDistortionSettings& settings) {
  LensResult result;
  if (!valid_settings(settings) || input.width == 0u || input.height == 0u ||
      input.pixels.size() != std::size_t(input.width) * input.height) {
    return result;
  }

  output = {input.width, input.height, {}};
  output.pixels.resize(input.pixels.size());

  for (std::uint32_t y = 0; y < input.height; ++y) {
    for (std::uint32_t x = 0; x < input.width; ++x) {
      const float u = static_cast<float>(x) /
                      static_cast<float>(std::max(1u, input.width - 1u));
      const float v = static_cast<float>(y) /
                      static_cast<float>(std::max(1u, input.height - 1u));
      float source_u = 0.0f;
      float source_v = 0.0f;
      map_coordinate(u, v, settings, source_u, source_v);

      LensPixel pixel;
      if (settings.chromatic_aberration > 0.0f) {
        const auto red = sample(input, source_u + settings.chromatic_aberration,
                                source_v, settings.edge_mode);
        const auto green = sample(input, source_u, source_v, settings.edge_mode);
        const auto blue = sample(input, source_u - settings.chromatic_aberration,
                                 source_v, settings.edge_mode);
        pixel = {red.r, green.g, blue.b, green.a};
      } else {
        pixel = sample(input, source_u, source_v, settings.edge_mode);
      }
      output.pixels[std::size_t(y) * input.width + x] = pixel;
    }
  }

  result.status = LensStatus::ready;
  result.digest = lens_frame_digest(output);
  return result;
}

LensResult dispatch_lens_distortion_gpu(const LensDispatchPacket& packet,
                                        const LensDispatch& dispatch) {
  LensResult result;
  if (packet.backend == LensBackend::cpu || packet.width == 0u ||
      packet.height == 0u || packet.input_handle == 0u ||
      packet.output_handle == 0u || packet.command_handle == 0u ||
      !valid_settings(packet.settings)) {
    return result;
  }
  if (!dispatch) {
    result.status = LensStatus::backend_unavailable;
    return result;
  }
  result.status =
      dispatch(packet) ? LensStatus::ready : LensStatus::dispatch_failed;
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_lens_distortion_rgba32f(
    const float* input, float* output, std::uint32_t width,
    std::uint32_t height, const DigitorLensDistortionSettings* settings,
    std::uint64_t* digest) {
  if (input == nullptr || output == nullptr || settings == nullptr ||
      digest == nullptr || width == 0u || height == 0u) {
    return 1u;
  }

  digitor::LensFrame input_frame{width, height, {}};
  input_frame.pixels.resize(std::size_t(width) * height);
  for (std::size_t index = 0; index < input_frame.pixels.size(); ++index) {
    input_frame.pixels[index] = {input[index * 4u], input[index * 4u + 1u],
                                 input[index * 4u + 2u],
                                 input[index * 4u + 3u]};
  }

  digitor::LensDistortionSettings native_settings;
  native_settings.mode = static_cast<digitor::LensMode>(settings->mode);
  native_settings.edge_mode =
      static_cast<digitor::LensEdgeMode>(settings->edge_mode);
  native_settings.k1 = settings->k1;
  native_settings.k2 = settings->k2;
  native_settings.k3 = settings->k3;
  native_settings.p1 = settings->p1;
  native_settings.p2 = settings->p2;
  native_settings.center_x = settings->center_x;
  native_settings.center_y = settings->center_y;
  native_settings.scale = settings->scale;
  native_settings.aspect = settings->aspect;
  native_settings.chromatic_aberration = settings->chromatic_aberration;
  native_settings.high_quality = settings->high_quality != 0u;

  digitor::LensFrame output_frame;
  const auto result = digitor::apply_lens_distortion_reference(
      input_frame, output_frame, native_settings);
  if (result.status != digitor::LensStatus::ready) {
    return 2u;
  }

  for (std::size_t index = 0; index < output_frame.pixels.size(); ++index) {
    output[index * 4u] = output_frame.pixels[index].r;
    output[index * 4u + 1u] = output_frame.pixels[index].g;
    output[index * 4u + 2u] = output_frame.pixels[index].b;
    output[index * 4u + 3u] = output_frame.pixels[index].a;
  }
  *digest = result.digest;
  return 0u;
}
