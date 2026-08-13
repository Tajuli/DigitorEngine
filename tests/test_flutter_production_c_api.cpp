#include "digitor/flutter_production_c_api.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

DigitorResult open_media(void*, const char* path, char*, std::uint32_t) {
  return path && *path ? DIGITOR_RESULT_OK : DIGITOR_RESULT_INVALID_ARGUMENT;
}

DigitorResult render_frame(void*, DigitorFlutterProductionRenderMode,
                           DigitorNodeGraph*, std::uint64_t, std::uint64_t,
                           std::int64_t, std::uint32_t, std::uint32_t,
                           DigitorNativeGpuTextureDescriptor*, char* diagnostic,
                           std::uint32_t capacity) {
  constexpr char message[] = "native D3D12 preview operation failed";
  if (diagnostic && capacity >= sizeof(message))
    std::memcpy(diagnostic, message, sizeof(message));
  return DIGITOR_RESULT_INTERNAL_ERROR;
}

DigitorResult export_media(void*, DigitorNodeGraph*, std::uint64_t,
                           std::uint64_t,
                           const DigitorFlutterExportRequest*,
                           DigitorExportProgressCallback, void*, char*,
                           std::uint32_t) {
  return DIGITOR_RESULT_UNSUPPORTED;
}

DigitorResult query_preview(void*, DigitorNativePreviewCapabilities* out) {
  return out ? DIGITOR_RESULT_OK : DIGITOR_RESULT_INVALID_ARGUMENT;
}

DigitorResult set_preview_target(void*, const DigitorFlutterPreviewTarget*,
                                 char*, std::uint32_t) {
  return DIGITOR_RESULT_OK;
}

DigitorResult cancel(void*) { return DIGITOR_RESULT_OK; }
void close_media(void*) {}
void release_texture(void*, const DigitorNativeGpuTextureDescriptor*) {}

}  // namespace

int main() {
  int owner = 7;
  DigitorFlutterProductionHost host{};
  host.struct_size = sizeof(host);
  host.api_version = DIGITOR_FLUTTER_PRODUCTION_HOST_VERSION;
  host.user_data = &owner;
  host.open_media = open_media;
  host.render_frame = render_frame;
  host.export_media = export_media;
  host.query_preview = query_preview;
  host.set_preview_target = set_preview_target;
  host.cancel = cancel;
  host.close_media = close_media;
  host.release_texture = release_texture;

  assert(digitor_flutter_production_host_registered() == 0);
  assert(digitor_flutter_production_register_host(&host) == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_host_registered() == 1);
  assert(digitor_flutter_production_register_host(&host) ==
         DIGITOR_RESULT_RESOURCE_IN_USE);

  DigitorFlutterProductionSession* session = nullptr;
  assert(digitor_flutter_production_create_registered("fixture.mov", &session) ==
         DIGITOR_RESULT_OK);
  assert(session != nullptr);

  DigitorNodeGraph* graph = nullptr;
  assert(digitor_node_graph_create(&graph) == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_bind_node_graph(session, graph, 1, 1) ==
         DIGITOR_RESULT_OK);
  DigitorNativeGpuTextureDescriptor texture{};
  assert(digitor_flutter_production_preview(session, 0, 16, 16, &texture) ==
         DIGITOR_RESULT_INTERNAL_ERROR);
  std::uint32_t diagnostic_size = 0;
  assert(digitor_flutter_production_get_last_error(
             session, nullptr, &diagnostic_size) == DIGITOR_RESULT_OK);
  std::string diagnostic(diagnostic_size, '\0');
  assert(digitor_flutter_production_get_last_error(
             session, diagnostic.data(), &diagnostic_size) == DIGITOR_RESULT_OK);
  assert(diagnostic.c_str() ==
         std::string("native D3D12 preview operation failed"));

  DigitorFlutterPreviewTarget target{};
  target.struct_size = sizeof(target);
  target.api_version = DIGITOR_FLUTTER_PREVIEW_TARGET_VERSION;
  target.native_target_handle = 1;
  target.width = target.height = 16;
  target.handle_type = DIGITOR_NATIVE_TEXTURE_HANDLE_DXGI_SHARED_HANDLE;
  assert(digitor_flutter_production_set_preview_target(session, &target) ==
         DIGITOR_RESULT_OK);
  diagnostic_size = 0;
  assert(digitor_flutter_production_get_last_error(
             session, nullptr, &diagnostic_size) == DIGITOR_RESULT_OK);
  assert(diagnostic_size == 1);

  // A plugin host cannot be detached while a Dart-facing production session
  // still owns it. This guards the Flutter hot-restart/detach lifetime rule.
  assert(digitor_flutter_production_unregister_host(&owner) ==
         DIGITOR_RESULT_RESOURCE_IN_USE);
  assert(digitor_flutter_production_destroy(session) == DIGITOR_RESULT_OK);
  assert(digitor_node_graph_destroy(graph) == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_unregister_host(&owner) == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_host_registered() == 0);

  return 0;
}
