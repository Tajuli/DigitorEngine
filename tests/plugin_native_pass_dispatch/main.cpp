#include "digitor/plugin_native_pass_dispatch.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_NATIVE_PASS_DISPATCH_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginGpuFrame frame(std::uint64_t handle) {
  digitor::PluginGpuFrame value{};
  value.backend = digitor::RemotePluginBackend::windows_d3d12;
  value.native_texture_handle = handle;
  value.synchronization_handle = 700;
  value.synchronization_value = handle;
  value.width = 1920;
  value.height = 1080;
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

  PluginGpuProgramRegistry registry;
  PluginBackendPackageLoaderBindings loader_bindings{};
  loader_bindings.selected_backend = RemotePluginBackend::windows_d3d12;
  loader_bindings.device_identity = 42;
  loader_bindings.read_asset = [](auto, auto, std::vector<std::byte>& bytes,
                                  std::string& diagnostic) {
    bytes = {std::byte{'D'}, std::byte{'X'}, std::byte{'I'}, std::byte{'L'}};
    diagnostic.clear();
    return true;
  };
  loader_bindings.create_pipeline = [](const PluginBackendAsset& asset,
                                       const PluginGpuProgram& program,
                                       PluginBackendPipeline& pipeline,
                                       std::string& diagnostic) {
    pipeline.package_identity = asset.package_identity;
    pipeline.plugin_id = asset.plugin_id;
    pipeline.plugin_version = asset.plugin_version;
    pipeline.backend = asset.backend;
    pipeline.format = asset.format;
    pipeline.native_pipeline_handle = 9001;
    pipeline.device_identity = 42;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  };
  loader_bindings.destroy_pipeline = [](const PluginBackendPipeline&) {};

  PluginBackendPackageLoader loader(registry, std::move(loader_bindings));
  PluginGpuProgram program{};
  program.plugin_id = "effect.remote.glow";
  program.plugin_version = "1.0.0";
  program.backend = RemotePluginBackend::windows_d3d12;
  program.format = PluginGpuProgramFormat::rgba16_float;
  program.package_identity = "sha256:glow-v1";
  PluginGpuPassDescriptor descriptor{};
  descriptor.entry_point = "main";
  descriptor.shader_asset = "shaders/glow.dxil";
  descriptor.bindings = {{"input", 0, false}, {"output", 1, true}};
  descriptor.workgroup_x = 8;
  descriptor.workgroup_y = 8;
  program.passes.push_back(descriptor);

  std::string diagnostic;
  if (loader.load("/installed/effect.remote.glow", program, &diagnostic) !=
      DIGITOR_RESULT_OK)
    return fail("package pipeline load failed");

  int recorded = 0;
  PluginNativeDispatch captured{};
  PluginNativePassDispatchBindings dispatch_bindings{};
  dispatch_bindings.selected_backend = RemotePluginBackend::windows_d3d12;
  dispatch_bindings.device_identity = 42;
  dispatch_bindings.record_dispatch = [&](const PluginNativeDispatch& value,
                                           std::string& local) {
    ++recorded;
    captured = value;
    local.clear();
    return DIGITOR_RESULT_OK;
  };
  PluginNativePassDispatcher dispatcher(loader, std::move(dispatch_bindings));

  PluginGpuDispatchPass pass{};
  pass.program = program;
  pass.pass = descriptor;
  pass.pass_index = 0;
  pass.input = frame(100);
  pass.output = frame(101);
  pass.parameters = {{"amount", 0.75}};

  if (dispatcher.record(pass, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("native pass record failed");
  if (recorded != 1 || captured.pipeline.native_pipeline_handle != 9001 ||
      captured.input.native_texture_handle != 100 ||
      captured.output.native_texture_handle != 101 ||
      captured.group_count_x != 240 || captured.group_count_y != 135 ||
      captured.parameters["amount"] != 0.75)
    return fail("native binding payload mismatch");

  auto wrong_device = pass;
  wrong_device.program.package_identity = "sha256:other";
  if (dispatcher.record(wrong_device, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("mismatched package identity was accepted");

  const auto telemetry = dispatcher.telemetry();
  if (telemetry.recorded_passes != 1 || telemetry.failed_passes != 1 ||
      telemetry.cpu_readbacks != 0 || telemetry.cpu_uploads != 0 ||
      telemetry.fallback_dispatches != 0)
    return fail("native pass telemetry mismatch");

  std::cout << "PLUGIN_NATIVE_PASS_DISPATCH=PASS\n";
  std::cout << "PIPELINE_HANDLE_BINDING=PASS\n";
  std::cout << "NATIVE_TEXTURE_SYNC_BINDING=PASS\n";
  std::cout << "DISPATCH_GEOMETRY=PASS\n";
  std::cout << "CPU_READBACKS=0\nCPU_UPLOADS=0\nFALLBACK_DISPATCHES=0\n";
  return 0;
}
