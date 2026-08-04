#include "digitor/native_text_backend_bridge.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace digitor;

namespace {

NativeTextBackendRequest request_for(NativeTextBackend backend, bool preview) {
  NativeTextBackendRequest request;
  request.backend = backend;
  request.handles = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
  request.upload.width = 64u;
  request.upload.height = 64u;
  request.upload.generation = 9u;
  request.upload.coverage.assign(64u * 64u, 255u);
  request.packet.atlas_generation = 9u;
  request.packet.gpu_ready = true;
  request.packet.vertices = {
      {0.0f, 0.0f, 0.0f, 0.0f, 0xffffffffu, 0u},
      {1.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0u},
      {1.0f, 1.0f, 1.0f, 1.0f, 0xffffffffu, 0u},
      {0.0f, 1.0f, 0.0f, 1.0f, 0xffffffffu, 0u},
  };
  request.packet.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  request.target_width = 1920u;
  request.target_height = 1080u;
  request.preview = preview;
  return request;
}

bool contains(const std::vector<NativeTextCommand>& commands, NativeTextCommandType type) {
  for (const auto& command : commands) {
    if (command.type == type) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  std::uint32_t submissions{};
  NativeTextBackendBridge bridge(
      [&submissions](NativeTextBackend, const NativeTextBackendHandles&,
                     const std::vector<NativeTextCommand>& commands) {
        ++submissions;
        return !commands.empty();
      });

  const std::array backends = {NativeTextBackend::vulkan, NativeTextBackend::d3d12,
                               NativeTextBackend::metal, NativeTextBackend::gles};
  for (const auto backend : backends) {
    const auto preview = bridge.submit(request_for(backend, true));
    if (preview.status != TextBackendStatus::ready ||
        !contains(preview.commands, NativeTextCommandType::upload_atlas) ||
        !contains(preview.commands, NativeTextCommandType::draw_indexed)) {
      return 1;
    }
    const auto export_result = bridge.submit(request_for(backend, false));
    if (export_result.status != TextBackendStatus::ready ||
        contains(export_result.commands, NativeTextCommandType::upload_atlas) ||
        export_result.digest != preview.digest) {
      return 2;
    }
    if (bridge.uploaded_generation(backend) != 9u) {
      return 3;
    }
  }

  auto stale = request_for(NativeTextBackend::vulkan, true);
  stale.packet.atlas_generation = 8u;
  if (bridge.submit(stale).status != TextBackendStatus::stale_packet) {
    return 4;
  }

  auto missing_handle = request_for(NativeTextBackend::d3d12, true);
  missing_handle.handles.pipeline = 0u;
  if (bridge.submit(missing_handle).status != TextBackendStatus::unavailable) {
    return 5;
  }

  NativeTextBackendBridge rejected(
      [](NativeTextBackend, const NativeTextBackendHandles&,
         const std::vector<NativeTextCommand>&) { return false; });
  if (rejected.submit(request_for(NativeTextBackend::metal, true)).status !=
      TextBackendStatus::draw_failed) {
    return 6;
  }

  bridge.invalidate();
  for (const auto backend : backends) {
    if (bridge.uploaded_generation(backend) != 0u) {
      return 7;
    }
  }

  if (submissions != 8u) {
    return 8;
  }

  std::cout << "BACKENDS=4\n"
            << "PREVIEW_EXPORT_PARITY=1\n"
            << "ATLAS_GENERATION_REUSE=1\n"
            << "STALE_PACKET_REJECTION=1\n"
            << "NO_SILENT_CPU_FALLBACK=1\n";
  return 0;
}
