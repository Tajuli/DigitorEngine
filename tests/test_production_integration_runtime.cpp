#include "digitor/production_integration_runtime.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace {
using namespace digitor;

[[noreturn]] void fail_check(const char* expression, int line) {
  std::cerr << "check failed at line " << line << ": " << expression << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) \
  ((expression) ? static_cast<void>(0) : fail_check(#expression, __LINE__))

NativeImplementationEvidence evidence(const char* identity) {
  NativeImplementationEvidence out{};
  out.production_implementation = true;
  out.native_api_bound = true;
  out.synchronization_bound = true;
  out.zero_copy_telemetry_bound = true;
  out.implementation_identity = identity;
  return out;
}

FlutterProductionProviderBuild complete_build() {
  FlutterProductionProviderBuild build{};
  build.provider.platform = ProductionPlatform::windows;
  build.provider.timeline = evidence("fixture.timeline.d3d12");
  build.provider.flutter_texture = evidence("fixture.flutter.d3d12");
  build.provider.encoder = evidence("fixture.encoder.mf");
  build.provider.package_identity = "fixture.windows.production";
  build.provider.build_identity = "fixture-build-1";
  build.provider.create = [](ProductionPlatformFactoryInputs inputs) {
    return create_production_platform_assembly(std::move(inputs));
  };

  static int shared_device_context;
  build.platform_inputs.platform = ProductionPlatform::windows;
  build.platform_inputs.timeline.backend = DIGITOR_RENDERER_D3D12;
  build.platform_inputs.timeline.context_identity = &shared_device_context;
  build.platform_inputs.timeline.device_identity = "fixture-d3d12-device";
  build.platform_inputs.timeline.create_target = [](
      std::uint32_t, std::uint32_t, std::int64_t)
      -> std::optional<ProcessedGpuFramePtr> { return std::nullopt; };
  build.platform_inputs.timeline.execute_effects = [](
      const VideoExecutionLayer&, const ProcessedGpuFramePtr&,
      ProcessedGpuFramePtr&, std::string&) {
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  };
  build.platform_inputs.timeline.composite_layer = [](
      const VideoExecutionLayer&, const ProcessedGpuFramePtr&,
      const ProcessedGpuFramePtr&, ProcessedGpuFramePtr&, std::string&) {
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  };
  build.platform_inputs.timeline.frame_evictable = [](
      const ProcessedGpuFrame&) { return true; };

  build.platform_inputs.flutter.platform = ProductionPlatform::windows;
  build.platform_inputs.flutter.backend = DIGITOR_RENDERER_D3D12;
  build.platform_inputs.flutter.device_identity = &shared_device_context;
  build.platform_inputs.flutter.device_name = "fixture-d3d12-device";
  build.platform_inputs.flutter.attached = [] { return true; };
  build.platform_inputs.flutter.register_or_present = [](
      const ProcessedGpuFramePtr&, std::uint64_t) {
    return DIGITOR_RESULT_OK;
  };

  build.decoder_factory = [](
      const std::string&, std::string& diagnostic)
      -> std::unique_ptr<ProductionHardwareDecodeSession> {
    diagnostic = "fixture decoder opens only in media-path tests";
    return nullptr;
  };
  build.frame_resolver = [](std::int64_t timestamp_us) {
    return static_cast<FrameNumber>(timestamp_us / 33333);
  };
  build.texture_descriptor_builder = [](
      const ProcessedGpuFramePtr&, std::uint64_t,
      DigitorNativeGpuTextureDescriptor&, std::string&) {
    return DIGITOR_RESULT_OK;
  };
  build.preview_target_binder = [](
      std::uint64_t, std::uint32_t, std::uint32_t, std::int32_t,
      std::string&) { return DIGITOR_RESULT_OK; };
  build.preview_capabilities.native_gpu_preview_available = 1;
  build.preview_capabilities.true_shared_resource_zero_copy = 1;
  build.preview_capabilities.backend = DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12;
  build.preview_capabilities.handle_type =
      DIGITOR_NATIVE_TEXTURE_HANDLE_D3D12_RESOURCE;
  build.encoder_backend = EncoderBackend::nvenc;
  build.fps_num = 30;
  build.fps_den = 1;
  build.video_bitrate = 12'000'000;
  build.required_device_identity = 0x1100;
  build.required_context_identity = 0x1100;
  return build;
}
}  // namespace

int main() {
  using namespace digitor;
  const auto platform = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS;
  REQUIRE(!flutter_production_provider_builder_installed(platform));

  // The authoritative validator rejects partial app-facing composition before
  // any process-wide factory can be reported as ready.
  auto incomplete_build = complete_build();
  incomplete_build.preview_target_binder = {};
  const auto incomplete_validation =
      validate_flutter_production_provider_build(platform, incomplete_build);
  REQUIRE(!incomplete_validation);
  REQUIRE(incomplete_validation.result == DIGITOR_RESULT_BACKEND_UNAVAILABLE);
  REQUIRE(incomplete_validation.diagnostic.find("preview target binder") !=
         std::string::npos);

  int registrar_token = 19;
  DigitorFlutterProductionPluginAttachment attachment{};
  attachment.struct_size = sizeof(attachment);
  attachment.api_version = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ATTACHMENT_VERSION;
  attachment.platform = platform;
  attachment.flutter_texture_registrar = &registrar_token;
  attachment.implementation_identity = "fixture.flutter.windows";

  // Attach-first is now a supported production startup order.
  REQUIRE(digitor_flutter_production_plugin_attach(&attachment) ==
         DIGITOR_RESULT_NOT_INITIALIZED);
  REQUIRE(digitor_flutter_production_plugin_attached() == 0);

  // An incomplete provider runtime cannot satisfy the waiting Flutter host and
  // therefore is not left nominally installed.
  std::string diagnostic;
  auto rejected = ProductionIntegrationRuntime::install(
      platform,
      [](const FlutterProductionPluginAttachment&, std::string&)
          -> std::optional<FlutterProductionProviderBuild> {
        auto build = complete_build();
        build.preview_target_binder = {};
        return build;
      },
      &diagnostic);
  REQUIRE(!rejected);
  REQUIRE(!diagnostic.empty());
  REQUIRE(!flutter_production_provider_builder_installed(platform));
  REQUIRE(digitor_flutter_production_plugin_attached() == 0);

  // A complete engine-owned runtime consumes the retained attachment and
  // auto-registers the host. The app never has to orchestrate a second attach.
  auto runtime = ProductionIntegrationRuntime::install(
      platform,
      [](const FlutterProductionPluginAttachment& input, std::string& local)
          -> std::optional<FlutterProductionProviderBuild> {
        REQUIRE(input.flutter_texture_registrar != nullptr);
        local.clear();
        return complete_build();
      },
      &diagnostic);
  REQUIRE(runtime && runtime->active());
  REQUIRE(diagnostic.empty());
  REQUIRE(flutter_production_provider_builder_installed(platform));
  REQUIRE(digitor_flutter_production_plugin_attached() == 1);
  REQUIRE(digitor_flutter_production_host_registered() == 1);
  REQUIRE(std::string(digitor_flutter_production_plugin_last_error()).empty());

  const auto generation = runtime->generation();
  REQUIRE(runtime->shutdown() == DIGITOR_RESULT_RESOURCE_IN_USE);
  REQUIRE(!runtime->active());
  REQUIRE(digitor_flutter_production_plugin_detach(&registrar_token) ==
         DIGITOR_RESULT_OK);
  REQUIRE(runtime->shutdown() == DIGITOR_RESULT_OK);
  REQUIRE(!flutter_production_provider_builder_installed(platform));

  // A later runtime generation can still install normally when Flutter is not
  // attached, preserving the existing explicit lifecycle contract.
  auto second = ProductionIntegrationRuntime::install(
      platform,
      [](const FlutterProductionPluginAttachment&, std::string&)
          -> std::optional<FlutterProductionProviderBuild> {
        return std::nullopt;
      });
  REQUIRE(second && second->generation() > generation);
  REQUIRE(second->shutdown() == DIGITOR_RESULT_OK);
  return 0;
}
