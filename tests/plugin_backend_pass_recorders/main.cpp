#include "digitor/plugin_backend_pass_recorders.hpp"

#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_BACKEND_PASS_RECORDERS_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginGpuFrame frame(std::uint64_t texture) {
  digitor::PluginGpuFrame value{};
  value.backend = digitor::RemotePluginBackend::windows_d3d12;
  value.native_texture_handle = texture;
  value.synchronization_handle = 700;
  value.synchronization_value = texture;
  value.width = 3840;
  value.height = 2160;
  value.format = digitor::PluginPixelFormat::rgba16_float;
  value.primaries = digitor::PluginColorPrimaries::bt2020;
  value.transfer = digitor::PluginTransferFunction::pq;
  value.range = digitor::PluginColorRange::full;
  value.alpha = digitor::PluginAlphaMode::straight;
  value.timestamp_us = 1000;
  return value;
}
}  // namespace

int main() {
  using namespace digitor;

  PluginNativeDispatch dispatch{};
  dispatch.pipeline.package_identity = "sha256:plugin-v1";
  dispatch.pipeline.plugin_id = "effect.remote.glow";
  dispatch.pipeline.plugin_version = "1.0.0";
  dispatch.pipeline.backend = RemotePluginBackend::windows_d3d12;
  dispatch.pipeline.format = PluginGpuProgramFormat::rgba16_float;
  dispatch.pipeline.native_pipeline_handle = 9001;
  dispatch.pipeline.device_identity = 42;
  dispatch.input = frame(100);
  dispatch.output = frame(101);
  dispatch.group_count_x = 480;
  dispatch.group_count_y = 270;
  dispatch.group_count_z = 1;
  dispatch.parameters = {{"amount", 0.8}};

  int d3d12_records = 0;
  PluginBackendPassRecorderBindings bindings{};
  bindings.selected_backend = RemotePluginBackend::windows_d3d12;
  bindings.device_identity = 42;
  bindings.command_context.backend = RemotePluginBackend::windows_d3d12;
  bindings.command_context.device_identity = 42;
  bindings.command_context.command_context_handle = 5001;
  bindings.command_context.provider_owned = true;
  bindings.command_context.external_synchronization = true;
  bindings.record_d3d12 = [&](const PluginNativeDispatch& value,
                              const PluginBackendCommandContext& context,
                              std::string& diagnostic) {
    if (value.pipeline.native_pipeline_handle != 9001 ||
        value.input.native_texture_handle != 100 ||
        value.output.native_texture_handle != 101 ||
        context.command_context_handle != 5001)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    ++d3d12_records;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  };

  PluginBackendPassRecorder recorder(std::move(bindings));
  std::string diagnostic;
  if (recorder.record(dispatch, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("D3D12 provider recording failed");
  if (d3d12_records != 1)
    return fail("D3D12 provider callback was not invoked");

  auto invalid = dispatch;
  invalid.output.synchronization_handle = 0;
  if (recorder.record(invalid, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("missing output synchronization was accepted");

  const auto telemetry = recorder.telemetry();
  if (telemetry.recorded_passes != 1 || telemetry.failed_passes != 1 ||
      telemetry.cpu_readbacks != 0 || telemetry.cpu_uploads != 0 ||
      telemetry.fallback_dispatches != 0)
    return fail("recorder telemetry mismatch");

  std::cout << "PLUGIN_BACKEND_PASS_RECORDERS=PASS\n";
  std::cout << "PROVIDER_OWNED_COMMAND_CONTEXT=PASS\n";
  std::cout << "NATIVE_RESOURCE_BINDING=PASS\n";
  std::cout << "EXTERNAL_SYNCHRONIZATION=PASS\n";
  std::cout << "CPU_READBACKS=0\nCPU_UPLOADS=0\nFALLBACK_DISPATCHES=0\n";
  return 0;
}
