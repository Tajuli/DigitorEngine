#include "digitor/production_transitions.hpp"

#include <cstdint>
#include <vector>

int main() {
  using namespace digitor;

  TransitionFrame a{2u, 1u, {{1.0f, 0.0f, 0.0f, 1.0f},
                              {1.0f, 0.0f, 0.0f, 1.0f}}};
  TransitionFrame b{2u, 1u, {{0.0f, 0.0f, 1.0f, 1.0f},
                              {0.0f, 0.0f, 1.0f, 1.0f}}};
  TransitionSettings settings;
  settings.progress = 0.5f;

  TransitionFrame preview;
  TransitionFrame export_frame;
  const auto preview_result = apply_transition_reference(a, b, preview, settings);
  const auto export_result = apply_transition_reference(a, b, export_frame, settings);
  if (preview_result.status != TransitionStatus::ready) {
    return 1;
  }
  if (preview_result.digest != export_result.digest) {
    return 2;
  }
  if (preview.pixels[0].r <= 0.0f || preview.pixels[0].b <= 0.0f) {
    return 3;
  }

  settings.type = TransitionType::dip_to_color;
  settings.progress = 0.5f;
  settings.dip_r = 0.0f;
  settings.dip_g = 0.0f;
  settings.dip_b = 0.0f;
  TransitionFrame dip;
  if (apply_transition_reference(a, b, dip, settings).status != TransitionStatus::ready) {
    return 4;
  }
  if (dip.pixels[0].r != 0.0f || dip.pixels[0].g != 0.0f ||
      dip.pixels[0].b != 0.0f) {
    return 5;
  }

  settings.type = TransitionType::wipe;
  settings.progress = 0.5f;
  settings.softness = 0.0f;
  settings.direction = TransitionDirection::right;
  TransitionFrame wipe;
  if (apply_transition_reference(a, b, wipe, settings).status != TransitionStatus::ready) {
    return 6;
  }

  settings.type = TransitionType::slide;
  settings.direction = TransitionDirection::left;
  TransitionFrame slide;
  if (apply_transition_reference(a, b, slide, settings).status != TransitionStatus::ready) {
    return 7;
  }

  TransitionDispatchPacket packet;
  packet.backend = TransitionBackend::vulkan;
  packet.width = 2u;
  packet.height = 1u;
  packet.input_a_handle = 1u;
  packet.input_b_handle = 2u;
  packet.output_handle = 3u;
  packet.command_handle = 4u;
  packet.settings = settings;
  if (dispatch_transition_gpu(packet, {}).status !=
      TransitionStatus::backend_unavailable) {
    return 8;
  }
  if (dispatch_transition_gpu(packet, [](const auto&) { return true; }).status !=
      TransitionStatus::ready) {
    return 9;
  }
  packet.command_handle = 0u;
  if (dispatch_transition_gpu(packet, [](const auto&) { return true; }).status !=
      TransitionStatus::invalid) {
    return 10;
  }

  std::vector<float> packed_a(8u);
  std::vector<float> packed_b(8u);
  std::vector<float> packed_output(8u);
  for (std::size_t index = 0; index < 2u; ++index) {
    packed_a[index * 4u] = 1.0f;
    packed_a[index * 4u + 3u] = 1.0f;
    packed_b[index * 4u + 2u] = 1.0f;
    packed_b[index * 4u + 3u] = 1.0f;
  }
  DigitorTransitionSettings c_settings{};
  c_settings.type = 0u;
  c_settings.direction = 0u;
  c_settings.progress = 0.5f;
  c_settings.softness = 0.02f;
  c_settings.dip_a = 1.0f;
  c_settings.ease_in_out = 1u;
  std::uint64_t digest{};
  if (digitor_transition_rgba32f(packed_a.data(),
                                  packed_b.data(),
                                  packed_output.data(),
                                  2u,
                                  1u,
                                  &c_settings,
                                  &digest) != 0u) {
    return 11;
  }
  if (digest != preview_result.digest) {
    return 12;
  }
  return 0;
}
