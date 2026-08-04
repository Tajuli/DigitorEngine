#include "digitor/plugin_gpu_program.hpp"

#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_GPU_PROGRAM_FAILED=" << message << '\n';
  return 1;
}
}

int main() {
  using namespace digitor;

  PluginGpuProgram program{};
  program.plugin_id = "effect.remote.cinematic";
  program.plugin_version = "1.0.0";
  program.backend = RemotePluginBackend::windows_d3d12;
  program.format = PluginGpuProgramFormat::rgba16_float;
  program.package_identity = "sha256:qualified-package";
  PluginGpuPassDescriptor pass{};
  pass.entry_point = "main";
  pass.shader_asset = "shaders/windows-d3d12.dxil";
  pass.bindings = {{"input", 0, false}, {"output", 1, true}};
  program.passes.push_back(pass);

  PluginGpuProgramRegistry registry;
  std::string diagnostic;
  if (registry.register_program(program, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("valid program registration failed");

  std::uint64_t recorded = 0;
  PluginGpuProgramRuntimeBindings bindings{};
  bindings.selected_backend = RemotePluginBackend::windows_d3d12;
  bindings.record_pass = [&](const PluginGpuDispatchPass& value,
                             std::string& local) {
    if (value.program.package_identity != "sha256:qualified-package" ||
        value.input.native_texture_handle != 10 ||
        value.output.native_texture_handle != 11 ||
        value.pass.entry_point != "main") {
      local = "dispatch payload mismatch";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    ++recorded;
    local.clear();
    return DIGITOR_RESULT_OK;
  };
  PluginGpuProgramRuntime runtime(registry, std::move(bindings));

  PluginZeroCopyRequest request{};
  request.instance.instance_id = "instance.1";
  request.instance.plugin_id = program.plugin_id;
  request.instance.plugin_version = program.plugin_version;
  request.instance.parameters["amount"] = 0.75;
  request.project_or_clip_id = "clip.1";
  request.visual_stack_digest = "stack-v1";
  request.input.backend = RemotePluginBackend::windows_d3d12;
  request.output.backend = RemotePluginBackend::windows_d3d12;
  request.input.native_texture_handle = 10;
  request.output.native_texture_handle = 11;
  request.input.width = request.output.width = 1920;
  request.input.height = request.output.height = 1080;
  request.input.format = request.output.format = PluginPixelFormat::rgba16_float;

  if (runtime.dispatch(request, &diagnostic) != DIGITOR_RESULT_OK ||
      recorded != 1)
    return fail("registered program did not dispatch");

  request.output.backend = RemotePluginBackend::windows_vulkan;
  if (runtime.dispatch(request, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("backend mismatch was accepted");

  PluginGpuProgram unsafe = program;
  unsafe.plugin_id = "effect.unsafe";
  unsafe.passes[0].preserves_alpha = false;
  if (registry.register_program(std::move(unsafe), &diagnostic) ==
      DIGITOR_RESULT_OK)
    return fail("non-alpha-preserving program was accepted");

  std::cout << "PLUGIN_GPU_PROGRAM=PASS\n";
  std::cout << "PACKAGE_TO_GPU_DISPATCH=PASS\n";
  std::cout << "GPU_FIRST=PASS\n";
  std::cout << "CPU_FALLBACK=NONE\n";
  return 0;
}
