#include "digitor/plugin_gpu_multipass.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_GPU_MULTIPASS_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginGpuFrame frame(std::uint64_t handle) {
  digitor::PluginGpuFrame value{};
  value.backend = digitor::RemotePluginBackend::windows_d3d12;
  value.native_texture_handle = handle;
  value.synchronization_handle = 500;
  value.synchronization_value = handle;
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

  PluginGpuProgramRegistry registry;
  PluginGpuProgram program{};
  program.plugin_id = "effect.remote.glow";
  program.plugin_version = "1.0.0";
  program.backend = RemotePluginBackend::windows_d3d12;
  program.format = PluginGpuProgramFormat::rgba16_float;
  program.package_identity = "sha256:remote-glow-v1";
  for (int index = 0; index < 3; ++index) {
    PluginGpuPassDescriptor pass{};
    pass.entry_point = "pass_" + std::to_string(index);
    pass.shader_asset = "shaders/windows-d3d12.dxil";
    pass.bindings = {{"input", 0, false}, {"output", 1, true}};
    program.passes.push_back(std::move(pass));
  }
  std::string diagnostic;
  if (registry.register_program(std::move(program), &diagnostic) !=
      DIGITOR_RESULT_OK)
    return fail("program registration failed");

  std::uint64_t next_handle = 20;
  std::vector<std::string> order;
  std::vector<std::uint64_t> released;
  int submissions = 0;
  PluginGpuMultiPassBindings bindings{};
  bindings.selected_backend = RemotePluginBackend::windows_d3d12;
  bindings.allocate_intermediate = [&](const PluginGpuFrame& prototype,
                                       PluginGpuFrame& output,
                                       std::string& local) {
    output = prototype;
    output.native_texture_handle = next_handle++;
    output.synchronization_handle = 900;
    output.synchronization_value = output.native_texture_handle;
    local.clear();
    return DIGITOR_RESULT_OK;
  };
  bindings.release_intermediate = [&](const PluginGpuFrame& value) {
    released.push_back(value.native_texture_handle);
  };
  bindings.record_pass = [&](const PluginGpuDispatchPass& pass,
                             std::string& local) {
    order.push_back(std::to_string(pass.pass_index) + ":" +
                    std::to_string(pass.input.native_texture_handle) + "->" +
                    std::to_string(pass.output.native_texture_handle));
    local.clear();
    return DIGITOR_RESULT_OK;
  };
  bindings.submit = [&](std::string& local) {
    ++submissions;
    local.clear();
    return DIGITOR_RESULT_OK;
  };

  PluginGpuMultiPassRuntime runtime(registry, std::move(bindings));
  PluginZeroCopyRequest request{};
  request.instance.instance_id = "instance.1";
  request.instance.plugin_id = "effect.remote.glow";
  request.instance.plugin_version = "1.0.0";
  request.instance.parameters = {{"amount", 0.8}};
  request.surface = ConsumerPluginSurface::preview;
  request.project_or_clip_id = "clip.1";
  request.visual_stack_digest = "stack-v1";
  request.input = frame(10);
  request.output = frame(30);

  if (runtime.dispatch(request, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("multi-pass dispatch failed");
  if (order.size() != 3 || order[0] != "0:10->20" ||
      order[1] != "1:20->21" || order[2] != "2:21->30")
    return fail("ordered GPU chain mismatch");
  if (submissions != 1 || released.size() != 2 ||
      released[0] != 21 || released[1] != 20)
    return fail("submission or reverse cleanup mismatch");

  const auto telemetry = runtime.telemetry();
  if (telemetry.dispatched_programs != 1 ||
      telemetry.recorded_passes != 3 ||
      telemetry.intermediate_allocations != 2 ||
      telemetry.submissions != 1 || telemetry.failed_programs != 0 ||
      telemetry.cpu_readbacks != 0 || telemetry.cpu_uploads != 0 ||
      telemetry.fallback_dispatches != 0)
    return fail("telemetry mismatch");

  std::cout << "PLUGIN_GPU_MULTIPASS=PASS\n";
  std::cout << "ORDERED_PASSES=3\n";
  std::cout << "GPU_INTERMEDIATES=2\n";
  std::cout << "SUBMISSIONS=1\n";
  std::cout << "CPU_READBACKS=0\nCPU_UPLOADS=0\nFALLBACK_DISPATCHES=0\n";
  return 0;
}
