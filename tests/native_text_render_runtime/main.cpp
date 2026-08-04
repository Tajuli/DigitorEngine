#include "digitor/native_text_render_runtime.hpp"

#include <cstdint>
#include <iostream>

using namespace digitor;

int main() {
  std::uint32_t uploads = 0u;
  std::uint32_t draws = 0u;
  NativeTextRenderRuntime runtime(
      [&](const AtlasUpload& atlas) {
        ++uploads;
        return atlas.coverage.size() == static_cast<std::size_t>(atlas.width) * atlas.height;
      },
      [&](const TextDrawPacket& packet, std::uint32_t width, std::uint32_t height) {
        ++draws;
        return packet.gpu_ready && width == 1920u && height == 1080u;
      });

  TextDrawPacket packet;
  packet.atlas_generation = 4u;
  packet.gpu_ready = true;
  packet.vertices = {{0.0f, 0.0f, 0.0f, 0.0f, 0xffffffffu, 0u},
                     {10.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0u},
                     {10.0f, 20.0f, 1.0f, 1.0f, 0xffffffffu, 0u},
                     {0.0f, 20.0f, 0.0f, 1.0f, 0xffffffffu, 0u}};
  packet.indices = {0u, 1u, 2u, 0u, 2u, 3u};

  TextRenderRequest preview;
  preview.packet = packet;
  preview.atlas.generation = 4u;
  preview.atlas.width = 8u;
  preview.atlas.height = 8u;
  preview.atlas.coverage.assign(64u, 255u);
  preview.consumer = TextRenderConsumer::preview;
  preview.target_width = 1920u;
  preview.target_height = 1080u;

  const auto first = runtime.render(preview);
  if (first.status != TextBackendStatus::ready || !first.atlas_uploaded || uploads != 1u || draws != 1u) return 1;

  auto export_request = preview;
  export_request.consumer = TextRenderConsumer::export_frame;
  const auto second = runtime.render(export_request);
  if (second.status != TextBackendStatus::ready || second.atlas_uploaded || uploads != 1u || draws != 2u) return 2;
  if (first.digest != second.digest) return 3;

  auto stale = preview;
  stale.packet.atlas_generation = 5u;
  if (runtime.render(stale).status != TextBackendStatus::stale_packet) return 4;

  const auto c_digest = digitor_text_packet_digest(packet.vertices.data(), packet.vertices.size(),
                                                    packet.indices.data(), packet.indices.size(),
                                                    packet.atlas_generation, 1920u, 1080u);
  if (c_digest != first.digest) return 5;

  NativeTextRenderRuntime unavailable({}, {});
  if (unavailable.render(preview).status != TextBackendStatus::unavailable) return 6;

  runtime.invalidate();
  if (runtime.uploaded_generation() != 0u) return 7;

  std::cout << "TEXT_ATLAS_UPLOAD=1\nTEXT_DRAW_SUBMISSION=1\nPREVIEW_EXPORT_PARITY=1\n"
               "STALE_PACKET_REJECTION=1\nGPU_REQUIRED_POLICY=1\nC_ABI_DIGEST=1\n";
  return 0;
}
