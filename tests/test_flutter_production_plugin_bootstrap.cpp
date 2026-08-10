#include "digitor/flutter_production_plugin_bootstrap.hpp"

#include <cassert>
#include <atomic>
#include <memory>

namespace {
using namespace digitor;

class TextureHost final : public NativePreviewTextureHost {
 public:
  bool attached() const noexcept override { return true; }
  DigitorRendererBackend backend() const noexcept override { return DIGITOR_RENDERER_D3D12; }
  const void* device_identity() const noexcept override { return reinterpret_cast<void*>(0x11); }
  DigitorResult present(const ProcessedGpuFramePtr&, std::uint64_t) noexcept override { return DIGITOR_RESULT_OK; }
};

class Decoder final : public VideoDecoder {
 public:
  std::shared_ptr<VideoFrame> decode(FrameNumber number) override {
    auto frame = std::make_shared<VideoFrame>();
    frame->number = number; frame->pts = number * 33333; frame->duration = 33333;
    frame->width = 16; frame->height = 16;
    NativeMediaSurfaceDescriptor d{}; d.struct_size = sizeof(d); d.api_version = 1;
    d.platform = NativeMediaPlatform::windows; d.handle_type = NativeMediaHandleType::d3d11_texture2d;
    d.pixel_format = NativeMediaPixelFormat::nv12; d.width = 16; d.height = 16;
    d.native_handle = 0x1000 + number;
    frame->native_surface = std::make_shared<NativeMediaSurface>(d, std::static_pointer_cast<void>(std::make_shared<int>(1)));
    return frame;
  }
  void seek(std::int64_t) override {}
  DecoderInfo info() const override { return {HardwareDecode::dxva, true, "fixture", true, NativeMediaHandleType::d3d11_texture2d}; }
};

std::unique_ptr<ProductionHardwareDecodeSession> decoder_session() {
  ProductionHardwareDecodeOptions options{}; options.renderer_backend = DIGITOR_RENDERER_D3D12;
  return std::make_unique<ProductionHardwareDecodeSession>(
      std::make_unique<Decoder>(), [](const ZeroCopyImportRequest& request, ProcessedGpuFramePtr& output) {
        GpuFrameMetadata m{}; m.width = 16; m.height = 16; m.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
        m.timestamp = request.surface->descriptor().timestamp_us;
        output = std::make_shared<ProcessedGpuFrame>(reinterpret_cast<void*>(0x22), DIGITOR_RENDERER_D3D12, m, 1,
            std::static_pointer_cast<void>(std::make_shared<int>(1)), std::make_shared<std::atomic_bool>(true), false);
        return DIGITOR_RESULT_OK;
      }, options);
}
}  // namespace

int main() {
  using namespace digitor;

  int registrar_token = 7;
  DigitorFlutterProductionPluginAttachment attachment{};
  attachment.struct_size = sizeof(attachment);
  attachment.api_version = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ATTACHMENT_VERSION;
  attachment.platform = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS;
  attachment.flutter_texture_registrar = &registrar_token;
  attachment.implementation_identity = "test.flutter.windows";

  assert(digitor_flutter_production_plugin_attach(&attachment) ==
         DIGITOR_RESULT_NOT_INITIALIZED);
  assert(digitor_flutter_production_plugin_attached() == 0);

  const auto installed = install_flutter_production_host_inputs_factory(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS,
      [](const FlutterProductionPluginAttachment& input, std::string& diagnostic)
          -> std::optional<FlutterProductionHostAdapterInputs> {
        assert(input.flutter_texture_registrar != nullptr);
        FlutterProductionHostAdapterInputs values{};
        values.decoder_factory = [](const std::string&, std::string& d)
            -> std::unique_ptr<ProductionHardwareDecodeSession> {
          d = "test factory intentionally has no decoder session";
          return nullptr;
        };
        values.frame_resolver = [](std::int64_t timestamp_us) {
          return static_cast<FrameNumber>(timestamp_us / 33333);
        };
        diagnostic.clear();
        return values;
      });
  assert(installed == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_plugin_attach(&attachment) ==
         DIGITOR_RESULT_NOT_INITIALIZED);
  assert(digitor_flutter_production_plugin_attached() == 0);
  assert(clear_flutter_production_host_inputs_factory(
             DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS) == DIGITOR_RESULT_OK);

  // A failed detach must retain plugin ownership while a registered session
  // is live, allowing the exact same registrar to retry after closing it.
  assert(install_flutter_production_host_inputs_factory(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS,
      [](const FlutterProductionPluginAttachment&, std::string& diagnostic)
          -> std::optional<FlutterProductionHostAdapterInputs> {
        FlutterProductionHostAdapterInputs v{};
        v.decoder_factory = [](const std::string&, std::string&) { return decoder_session(); };
        v.frame_resolver = [](std::int64_t t) { return static_cast<FrameNumber>(t / 33333); };
        v.preview_session = std::make_shared<NativePreviewPresentationSession>(std::make_shared<TextureHost>());
        v.texture_descriptor_builder = [](const ProcessedGpuFramePtr&, std::uint64_t, DigitorNativeGpuTextureDescriptor&, std::string&) { return DIGITOR_RESULT_OK; };
        v.preview_target_binder = [](std::uint64_t, std::uint32_t, std::uint32_t, std::int32_t, std::string&) { return DIGITOR_RESULT_OK; };
        v.preview_capabilities.native_gpu_preview_available = 1;
        v.preview_capabilities.true_shared_resource_zero_copy = 1;
        v.preview_capabilities.backend = DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12;
        v.preview_capabilities.handle_type = DIGITOR_NATIVE_TEXTURE_HANDLE_D3D12_RESOURCE;
        diagnostic.clear(); return v;
      }) == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_plugin_attach(&attachment) == DIGITOR_RESULT_OK);
  DigitorFlutterProductionSession* session = nullptr;
  assert(digitor_flutter_production_create_registered("fixture.mp4", &session) == DIGITOR_RESULT_OK);
  assert(session != nullptr && digitor_flutter_production_host_registered() == 1);
  assert(digitor_flutter_production_plugin_detach(&registrar_token) == DIGITOR_RESULT_RESOURCE_IN_USE);
  assert(digitor_flutter_production_plugin_attached() == 1);
  assert(digitor_flutter_production_host_registered() == 1);
  digitor_flutter_production_destroy(session);
  assert(digitor_flutter_production_plugin_detach(&registrar_token) == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_plugin_attached() == 0);
  assert(digitor_flutter_production_host_registered() == 0);
  assert(clear_flutter_production_host_inputs_factory(DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS) == DIGITOR_RESULT_OK);

  DigitorFlutterProductionPluginAttachment invalid{};
  assert(digitor_flutter_production_plugin_attach(&invalid) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(digitor_flutter_production_plugin_detach(&registrar_token) ==
         DIGITOR_RESULT_OK);
  return 0;
}
