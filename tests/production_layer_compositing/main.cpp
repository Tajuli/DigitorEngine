#include "digitor/production_layer_compositing.hpp"

#include <cstdint>
#include <vector>

int main() {
  using namespace digitor;

  CompositeFrame background{1u, 1u, {{0.2f, 0.4f, 0.6f, 1.0f}}};
  CompositeFrame foreground{1u, 1u, {{1.0f, 0.0f, 0.0f, 0.5f}}};
  CompositeSettings settings;

  CompositeFrame preview;
  CompositeFrame export_frame;
  const auto preview_result =
      composite_reference(background, foreground, preview, settings);
  const auto export_result =
      composite_reference(background, foreground, export_frame, settings);
  if (preview_result.status != CompositeStatus::ready ||
      preview_result.digest != export_result.digest) {
    return 1;
  }
  if (preview.pixels[0].a != 1.0f || preview.pixels[0].r <= 0.2f) {
    return 2;
  }

  settings.blend_mode = BlendMode::multiply;
  CompositeFrame multiply;
  if (composite_reference(background, foreground, multiply, settings).status !=
      CompositeStatus::ready) {
    return 3;
  }

  settings.blend_mode = BlendMode::screen;
  settings.opacity = 0.25f;
  CompositeFrame screen;
  if (composite_reference(background, foreground, screen, settings).status !=
      CompositeStatus::ready) {
    return 4;
  }

  CompositeDispatchPacket packet;
  packet.backend = CompositeBackend::vulkan;
  packet.width = 1u;
  packet.height = 1u;
  packet.background_handle = 1u;
  packet.foreground_handle = 2u;
  packet.output_handle = 3u;
  packet.command_handle = 4u;
  packet.settings = settings;
  if (dispatch_composite_gpu(packet, {}).status !=
      CompositeStatus::backend_unavailable) {
    return 5;
  }
  if (dispatch_composite_gpu(packet, [](const auto&) { return true; }).status !=
      CompositeStatus::ready) {
    return 6;
  }
  packet.command_handle = 0u;
  if (dispatch_composite_gpu(packet, [](const auto&) { return true; }).status !=
      CompositeStatus::invalid) {
    return 7;
  }

  const std::vector<float> packed_background{0.2f, 0.4f, 0.6f, 1.0f};
  const std::vector<float> packed_foreground{1.0f, 0.0f, 0.0f, 0.5f};
  std::vector<float> packed_output(4u);
  DigitorCompositeSettings c_settings{};
  c_settings.blend_mode = 0u;
  c_settings.input_alpha = 0u;
  c_settings.output_alpha = 0u;
  c_settings.opacity = 1.0f;
  std::uint64_t digest{};
  if (digitor_composite_rgba32f(
          packed_background.data(), packed_foreground.data(),
          packed_output.data(), 1u, 1u, &c_settings, &digest) != 0u ||
      digest == 0u) {
    return 8;
  }
  return 0;
}
