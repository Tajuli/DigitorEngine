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

  digitor::FlutterProductionHostAdapter adapter(std::move(inputs));
  assert(adapter.valid());
  const auto host = adapter.host();
  assert(host.user_data != nullptr);
  assert(host.open_media && host.render_frame && host.export_media &&
         host.query_preview && host.cancel && host.close_media &&
         host.release_texture);

  char diagnostic[128]{};
  const auto open_result = host.open_media(
      host.user_data, "fixture.mp4", diagnostic, sizeof(diagnostic));
  assert(open_result == DIGITOR_RESULT_BACKEND_UNAVAILABLE);
  assert(std::string(diagnostic).find("intentionally unavailable") != std::string::npos);
  return 0;
}
