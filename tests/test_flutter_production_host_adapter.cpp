#include "digitor/flutter_production_host_adapter.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace {
class DummyTextureHost final : public digitor::NativePreviewTextureHost {
 public:
  bool attached() const noexcept override { return true; }
  DigitorRendererBackend backend() const noexcept override {
    return DIGITOR_RENDERER_D3D12;
  }
  const void* device_identity() const noexcept override {
    return reinterpret_cast<const void*>(0x1);
  }
  DigitorResult present(const digitor::ProcessedGpuFramePtr&,
                        std::uint64_t) noexcept override {
    return DIGITOR_RESULT_OK;
  }
 };

struct EndToEndHostState {
  int opens{};
  int renders{};
  int exports{};
  int cancels{};
  int closes{};
  int releases{};
  int target_binds{};
  std::uint64_t generation{};
};

DigitorResult open_media(void* user_data, const char* path, char*, std::uint32_t) {
  auto* state = static_cast<EndToEndHostState*>(user_data);
  if (!state || !path || !*path) return DIGITOR_RESULT_INVALID_ARGUMENT;
  ++state->opens;
  return DIGITOR_RESULT_OK;
}

DigitorResult render_frame(
    void* user_data, DigitorFlutterProductionRenderMode,
    DigitorNodeGraph* graph, std::uint64_t graph_revision,
    std::uint64_t parameter_revision, std::int64_t timestamp_us,
    std::uint32_t width, std::uint32_t height,
    DigitorNativeGpuTextureDescriptor* output, char*, std::uint32_t) {
  auto* state = static_cast<EndToEndHostState*>(user_data);
  if (!state || !graph || !graph_revision || !parameter_revision || !output)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  ++state->renders;
  output->struct_size = sizeof(*output);
  output->api_version = DIGITOR_NATIVE_GPU_TEXTURE_DESCRIPTOR_VERSION;
  output->backend = DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12;
  output->handle_type = DIGITOR_NATIVE_TEXTURE_HANDLE_D3D12_RESOURCE;
  output->native_handle = 0xCAFEu;
  output->width = width;
  output->height = height;
  output->timestamp_us = timestamp_us;
  output->generation = ++state->generation;
  output->device_identity = 0x11u;
  output->context_identity = 0x22u;
  output->readiness = DIGITOR_NATIVE_TEXTURE_READY;
  return DIGITOR_RESULT_OK;
}

DigitorResult export_media(
    void* user_data, DigitorNodeGraph* graph, std::uint64_t graph_revision,
    std::uint64_t parameter_revision, const DigitorFlutterExportRequest* request,
    DigitorExportProgressCallback progress, void* progress_user_data,
    char*, std::uint32_t) {
  auto* state = static_cast<EndToEndHostState*>(user_data);
  if (!state || !graph || !graph_revision || !parameter_revision || !request)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  ++state->exports;
  if (progress) progress(1.0, 1, 1, progress_user_data);
  return DIGITOR_RESULT_OK;
}

DigitorResult query_preview(void*, DigitorNativePreviewCapabilities* output) {
  if (!output) return DIGITOR_RESULT_INVALID_ARGUMENT;
  output->struct_size = sizeof(*output);
  output->api_version = DIGITOR_NATIVE_PREVIEW_CAPABILITIES_VERSION;
  output->native_gpu_preview_available = 1;
  output->true_shared_resource_zero_copy = 1;
  output->backend = DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12;
  output->handle_type = DIGITOR_NATIVE_TEXTURE_HANDLE_D3D12_RESOURCE;
  return DIGITOR_RESULT_OK;
}

DigitorResult set_preview_target(
    void* user_data, const DigitorFlutterPreviewTarget* target, char*,
    std::uint32_t) {
  auto* state = static_cast<EndToEndHostState*>(user_data);
  if (!state || !target || !target->native_target_handle || !target->width ||
      !target->height) return DIGITOR_RESULT_INVALID_ARGUMENT;
  ++state->target_binds;
  return DIGITOR_RESULT_OK;
}

DigitorResult cancel(void* user_data) {
  auto* state = static_cast<EndToEndHostState*>(user_data);
  if (!state) return DIGITOR_RESULT_INVALID_ARGUMENT;
  ++state->cancels;
  return DIGITOR_RESULT_OK;
}

void close_media(void* user_data) {
  auto* state = static_cast<EndToEndHostState*>(user_data);
  if (state) ++state->closes;
}

void release_texture(void* user_data, const DigitorNativeGpuTextureDescriptor*) {
  auto* state = static_cast<EndToEndHostState*>(user_data);
  if (state) ++state->releases;
}
}  // namespace

int main() {
  digitor::FlutterProductionHostAdapterInputs inputs{};
  inputs.decoder_factory = [](const std::string&, std::string& diagnostic)
      -> std::unique_ptr<digitor::ProductionHardwareDecodeSession> {
    diagnostic = "decoder intentionally unavailable in smoke test";
    return {};
  };
  inputs.frame_resolver = [](std::int64_t timestamp_us) {
    return static_cast<digitor::FrameNumber>(timestamp_us / 33'333);
  };
  inputs.preview_session = std::make_shared<digitor::NativePreviewPresentationSession>(
      std::make_shared<DummyTextureHost>());
  inputs.preview_target_binder = [](std::uint64_t, std::uint32_t, std::uint32_t, std::int32_t, std::string&) { return DIGITOR_RESULT_OK; };
  inputs.texture_descriptor_builder = [](
      const digitor::ProcessedGpuFramePtr&, std::uint64_t,
      DigitorNativeGpuTextureDescriptor&, std::string&) {
    return DIGITOR_RESULT_OK;
  };
  inputs.encoder_backend = digitor::EncoderBackend::nvenc;
  inputs.encoder_callbacks.open = [](const digitor::HardwareEncodeConfig&, std::string&) {
    return DIGITOR_RESULT_OK;
  };
  inputs.encoder_callbacks.submit_gpu_frame = [](
      const digitor::HardwareEncodeFrame&, std::string&) {
    return DIGITOR_RESULT_OK;
  };
  inputs.encoder_callbacks.drain = [](std::string&) { return DIGITOR_RESULT_OK; };
  inputs.encoder_callbacks.finalize_atomic = [](std::string&) { return DIGITOR_RESULT_OK; };
  inputs.encoder_callbacks.cancel = [] {};
  inputs.preview_capabilities.native_gpu_preview_available = 1;
  inputs.preview_capabilities.true_shared_resource_zero_copy = 1;
  inputs.preview_capabilities.backend = DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12;
  inputs.preview_capabilities.handle_type = DIGITOR_NATIVE_TEXTURE_HANDLE_D3D12_RESOURCE;

  assert(digitor_flutter_production_host_registered() == 0);
  {
    digitor::RegisteredFlutterProductionHost registration(std::move(inputs));
    assert(registration.result() == DIGITOR_RESULT_OK);
    assert(registration.registered());
    assert(digitor_flutter_production_host_registered() == 1);

    DigitorFlutterProductionSession* session = nullptr;
    const auto create_result = digitor_flutter_production_create_registered(
        "fixture.mp4", &session);
    assert(create_result == DIGITOR_RESULT_BACKEND_UNAVAILABLE);
    assert(session == nullptr);

    // The process-global registration is exclusive and cannot be replaced by
    // an incomplete/foreign owner while the production plugin owns it.
    DigitorFlutterProductionHost invalid{};
    assert(digitor_flutter_production_register_host(&invalid) ==
           DIGITOR_RESULT_INVALID_ARGUMENT);
  }
  assert(digitor_flutter_production_host_registered() == 0);

  EndToEndHostState state{};
  DigitorFlutterProductionHost host{};
  host.struct_size = sizeof(host);
  host.api_version = DIGITOR_FLUTTER_PRODUCTION_HOST_VERSION;
  host.user_data = &state;
  host.required_device_identity = 0x11u;
  host.required_context_identity = 0x22u;
  host.open_media = &open_media;
  host.render_frame = &render_frame;
  host.export_media = &export_media;
  host.query_preview = &query_preview;
  host.set_preview_target = &set_preview_target;
  host.cancel = &cancel;
  host.close_media = &close_media;
  host.release_texture = &release_texture;
  assert(digitor_flutter_production_register_host(&host) == DIGITOR_RESULT_OK);

  DigitorFlutterProductionSession* session = nullptr;
  assert(digitor_flutter_production_create_registered("fixture.mp4", &session) ==
         DIGITOR_RESULT_OK);
  assert(session != nullptr && state.opens == 1);

  DigitorNodeGraph* graph = nullptr;
  assert(digitor_node_graph_create(&graph) == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_bind_node_graph(session, graph, 1, 1) ==
         DIGITOR_RESULT_OK);

  DigitorNativePreviewCapabilities caps{};
  caps.struct_size = sizeof(caps);
  assert(digitor_flutter_production_query_preview(session, &caps) ==
         DIGITOR_RESULT_OK);
  assert(caps.native_gpu_preview_available == 1);

  DigitorFlutterPreviewTarget target{};
  target.struct_size = sizeof(target);
  target.api_version = DIGITOR_FLUTTER_PREVIEW_TARGET_VERSION;
  target.native_target_handle = 0x999u;
  target.width = 1920;
  target.height = 1080;
  target.handle_type = DIGITOR_NATIVE_TEXTURE_HANDLE_D3D12_RESOURCE;
  assert(digitor_flutter_production_set_preview_target(session, &target) ==
         DIGITOR_RESULT_OK);
  assert(state.target_binds == 1);

  DigitorNativeGpuTextureDescriptor texture{};
  assert(digitor_flutter_production_preview(session, 123456, 1920, 1080,
                                            &texture) == DIGITOR_RESULT_OK);
  assert(texture.generation == 1 && state.renders == 1);
  assert(digitor_flutter_production_preview_consumed(session,
                                                     texture.generation) ==
         DIGITOR_RESULT_OK);
  assert(state.releases == 1);

  DigitorFlutterExportRequest request{};
  request.struct_size = sizeof(request);
  request.api_version = DIGITOR_FLUTTER_EXPORT_REQUEST_VERSION;
  request.utf8_output_path = "out.mp4";
  request.first_frame = 0;
  request.last_frame = 0;
  request.width = 1920;
  request.height = 1080;
  assert(digitor_flutter_production_export(session, &request, nullptr, nullptr) ==
         DIGITOR_RESULT_OK);
  assert(state.exports == 1);
  assert(digitor_flutter_production_cancel(session) == DIGITOR_RESULT_OK);
  assert(state.cancels == 1);

  std::uint32_t error_size = 0;
  assert(digitor_flutter_production_get_last_error(session, nullptr,
                                                   &error_size) ==
         DIGITOR_RESULT_OK);
  assert(error_size == 1);

  assert(digitor_flutter_production_destroy(session) == DIGITOR_RESULT_OK);
  assert(state.closes == 1);
  assert(digitor_node_graph_destroy(graph) == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_unregister_host(&state) == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_host_registered() == 0);
  return 0;
}
