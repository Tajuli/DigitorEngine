#include "digitor/spatial_compositor.hpp"
#include "digitor/spatial_compositor_c.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace digitor;

namespace {
bool require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return false;
  }
  return true;
}
}  // namespace

int main() {
  FrameF foreground{2, 2,
                    {{0, 1, 0, 1}, {1, 0, 0, 1},
                     {0, 0, 1, 1}, {1, 1, 1, 1}}};
  FrameF background{4, 4, std::vector<PixelF>(16, {0.1f, 0.1f, 0.1f, 1})};

  LayerSettings layer;
  layer.spatial.transform.position = {0, 0};
  layer.spatial.transform.scale = {1, 1};
  layer.chroma_key.enabled = true;
  layer.chroma_key.similarity = .25;
  layer.chroma_key.softness = .2;

  StabilizationState stabilization;
  stabilization.enabled = true;
  stabilization.strength = 1;
  stabilization.crop_ratio = .05;
  std::vector<MotionSample> motion{{0, 0, 0, 0, 1}, {10, .1, 0, 2, 1}};

  RenderPolicy cpu{VisualBackend::cpu, false, true, true};
  auto preview = render_layer(foreground, background, layer, stabilization,
                              motion, 5, cpu);
  auto export_frame = render_layer(
      foreground, background, layer, stabilization, motion, 5,
      {VisualBackend::cpu, false, true, false});
  if (!require(preview.frame.valid(), "preview frame invalid") ||
      !require(stable_frame_digest(preview.frame) ==
                   stable_frame_digest(export_frame.frame),
               "preview/export digest mismatch")) {
    return 1;
  }

  auto denied = render_spatial(
      foreground, 4, 4, layer.spatial,
      {VisualBackend::vulkan, false, false, true});
  if (!require(!denied.frame.valid(), "GPU fallback policy was not enforced")) {
    return 2;
  }

  AnimatedScalar animation{0, {{0, 0}, {10, 100}}};
  if (!require(std::abs(evaluate(animation, 5) - 50) < 1e-9,
               "animation interpolation failed")) {
    return 3;
  }

  FrameF keyed = foreground;
  apply_chroma_key(keyed, layer.chroma_key);
  if (!require(keyed.pixels[0].a < .01f, "green screen was not removed") ||
      !require(keyed.pixels[1].a > .9f, "foreground alpha was damaged")) {
    return 4;
  }

  FrameF normal = background;
  FrameF overlay{4, 4, std::vector<PixelF>(16, {1, 0, 0, .5f})};
  composite(normal, overlay, BlendMode::normal, 1);
  if (!require(normal.pixels[0].r > .5f, "alpha compositing failed")) {
    return 5;
  }

  DigitorPixelF input[4] = {{1, 0, 0, 1}, {0, 1, 0, 1},
                            {0, 0, 1, 1}, {1, 1, 1, 1}};
  DigitorPixelF output[16]{};
  DigitorSpatialParams params{};
  params.scale_x = params.scale_y = 1;
  params.anchor_x = params.anchor_y = .5;
  params.opacity = 1;
  params.crop_right = params.crop_bottom = 1;
  params.bilinear = 1;

  const int c_api_ok = digitor_render_spatial_rgba32f(
      input, 2, 2, output, 4, 4, &params, 4, 0, 1);
  const int c_api_denied = digitor_render_spatial_rgba32f(
      input, 2, 2, output, 4, 4, &params, 0, 0, 0);
  if (!require(c_api_ok == DIGITOR_SPATIAL_OK, "C ABI render failed") ||
      !require(c_api_denied == DIGITOR_SPATIAL_GPU_UNAVAILABLE,
               "C ABI GPU policy failed")) {
    return 6;
  }

  std::cout << "SPATIAL_TRANSFORM_CROP=1\n"
            << "CHROMA_KEY_DESPILL=1\n"
            << "ALPHA_COMPOSITOR=1\n"
            << "STABILIZATION_TRACK_APPLICATION=1\n"
            << "ANIMATED_PROPERTIES=1\n"
            << "C_ABI=1\n"
            << "PREVIEW_EXPORT_PARITY=1\n"
            << "GPU_FALLBACK_POLICY=1\n";
  return 0;
}
