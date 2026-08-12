#include "digitor/flutter_production_c_api.h"

#include <cassert>
#include <cstdint>

namespace {

DigitorResult open_media(void*, const char* path, char*, std::uint32_t) {
  return path && *path ? DIGITOR_RESULT_OK : DIGITOR_RESULT_INVALID_ARGUMENT;
}

DigitorResult render_frame(void*, DigitorFlutterProductionRenderMode,
                           DigitorNodeGraph*, std::uint64_t, std::uint64_t,
                           std::int64_t, std::uint32_t, std::uint32_t,
                           DigitorNativeGpuTextureDescriptor*, char*,
                           std::uint32_t) {
  return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
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

  // A plugin host cannot be detached while a Dart-facing production session
  // still owns it. This guards the Flutter hot-restart/detach lifetime rule.
  assert(digitor_flutter_production_unregister_host(&owner) ==
         DIGITOR_RESULT_RESOURCE_IN_USE);
  assert(digitor_flutter_production_destroy(session) == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_unregister_host(&owner) == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_host_registered() == 0);

  return 0;
}
