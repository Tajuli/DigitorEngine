#include "digitor/production_masks.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

int main() {
  using namespace digitor;
  MaskDefinition rectangle;
  rectangle.shape = MaskShape::rectangle;
  rectangle.combine = MaskCombine::replace;
  rectangle.points = {{0.2f, 0.2f}, {0.8f, 0.8f}};
  rectangle.feather = 0.04f;

  MaskDefinition ellipse;
  ellipse.shape = MaskShape::ellipse;
  ellipse.combine = MaskCombine::subtract;
  ellipse.points = {{0.5f, 0.5f}, {0.15f, 0.15f}};

  MaskFrame preview, exported;
  const std::vector<MaskDefinition> masks{rectangle, ellipse};
  const auto a = render_masks_reference(64, 64, masks, preview);
  const auto b = render_masks_reference(64, 64, masks, exported);
  if (a.status != MaskStatus::ready || b.status != MaskStatus::ready) return 1;
  if (a.digest != b.digest || preview.alpha != exported.alpha) return 2;
  if (!(a.coverage > 0.1f && a.coverage < 0.8f)) return 3;
  const float center = preview.alpha[32u * 64u + 32u];
  const float outer = preview.alpha[8u * 64u + 8u];
  if (!(center < 0.1f && outer < 0.1f)) return 4;

  MaskDispatchPacket packet;
  packet.backend = MaskBackend::vulkan;
  packet.width = 64; packet.height = 64; packet.mask_count = 2;
  packet.output_handle = 1; packet.command_handle = 2; packet.definitions_handle = 3;
  if (dispatch_masks_gpu(packet, {}).status != MaskStatus::backend_unavailable) return 5;
  if (dispatch_masks_gpu(packet, [](const auto&) { return true; }).status != MaskStatus::ready) return 6;
  packet.command_handle = 0;
  if (dispatch_masks_gpu(packet, [](const auto&) { return true; }).status != MaskStatus::invalid) return 7;

  const DigitorMaskPoint points[] = {{0.2f, 0.2f}, {0.8f, 0.8f}, {0.5f, 0.5f}, {0.15f, 0.15f}};
  DigitorMaskDefinition defs[2]{};
  defs[0] = {0u, 0u, 0u, 2u, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.04f, 0.0f, 1.0f, 0u};
  defs[1] = {1u, 2u, 2u, 2u, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0u};
  std::vector<float> alpha(64u * 64u);
  std::uint64_t digest{};
  if (digitor_render_masks_f32(64, 64, defs, 2, points, 4, alpha.data(), &digest) != 0u) return 8;
  if (digest != a.digest) return 9;
  return 0;
}
