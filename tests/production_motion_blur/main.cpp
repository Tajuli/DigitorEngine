#include "digitor/production_motion_blur.hpp"

#include <vector>

int main() {
  using namespace digitor;

  MotionBlurFrame input{3, 1, {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}}};
  std::vector<MotionVector> motion(3, {2, 0, 1});
  MotionBlurSettings settings;
  settings.samples = 5;

  MotionBlurFrame preview;
  MotionBlurFrame export_frame;
  const auto preview_result = apply_motion_blur_reference(input, motion, preview, settings);
  const auto export_result = apply_motion_blur_reference(input, motion, export_frame, settings);

  if (preview_result.status != MotionBlurStatus::ready) {
    return 1;
  }
  if (export_result.status != MotionBlurStatus::ready) {
    return 2;
  }
  if (preview_result.digest != export_result.digest) {
    return 3;
  }
  if (preview.pixels.size() < 2u || preview.pixels[1].r <= 0.0f || preview.pixels[1].b <= 0.0f) {
    return 4;
  }

  MotionBlurDispatchPacket packet;
  packet.backend = MotionBlurBackend::vulkan;
  packet.width = 3;
  packet.height = 1;
  packet.input_handle = 1;
  packet.motion_handle = 2;
  packet.output_handle = 3;
  packet.command_handle = 4;
  packet.settings = settings;

  if (dispatch_motion_blur_gpu(packet, {}).status != MotionBlurStatus::backend_unavailable) {
    return 5;
  }
  if (dispatch_motion_blur_gpu(packet, [](const auto&) { return true; }).status !=
      MotionBlurStatus::ready) {
    return 6;
  }

  packet.command_handle = 0;
  if (dispatch_motion_blur_gpu(packet, [](const auto&) { return true; }).status !=
      MotionBlurStatus::invalid) {
    return 7;
  }

  return 0;
}
