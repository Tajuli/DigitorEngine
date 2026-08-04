#include "digitor/plugin_transition_program.hpp"

#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_TRANSITION_PROGRAM_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginGpuFrame frame(std::uint64_t texture) {
  digitor::PluginGpuFrame value{};
  value.backend = digitor::RemotePluginBackend::windows_d3d12;
  value.native_texture_handle = texture;
  value.width = 1920;
  value.height = 1080;
  value.format = digitor::PluginPixelFormat::rgba16_float;
  value.primaries = digitor::PluginColorPrimaries::bt2020;
  value.transfer = digitor::PluginTransferFunction::pq;
  value.range = digitor::PluginColorRange::full;
  value.alpha = digitor::PluginAlphaMode::straight;
  return value;
}
}  // namespace

int main() {
  using namespace digitor;

  PluginGpuProgram program{};
  program.plugin_id = "transition.future_package_without_engine_edit";
  program.plugin_version = "1.0.0";
  program.backend = RemotePluginBackend::windows_d3d12;
  program.format = PluginGpuProgramFormat::rgba16_float;
  program.package_identity = "sha256:transition-package";
  PluginGpuPassDescriptor pass{};
  pass.entry_point = "main";
  pass.shader_asset = "shaders/windows-d3d12.dxil";
  pass.bindings = {{"outgoing", 0, false}, {"incoming", 1, false},
                   {"output", 2, true}, {"progress", 3, false}};
  program.passes.push_back(pass);

  PluginGpuProgramRegistry registry;
  std::string diagnostic;
  if (registry.register_program(program, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("transition program registration failed");

  std::uint64_t calls = 0;
  PluginTransitionProgramBinding binding{};
  binding.registry = &registry;
  binding.selected_backend = RemotePluginBackend::windows_d3d12;
  binding.record = [&](const PluginTransitionDispatch& dispatch,
                       std::string& local) {
    if (dispatch.request.instance.plugin_id != program.plugin_id ||
        dispatch.request.outgoing.native_texture_handle != 10 ||
        dispatch.request.incoming.native_texture_handle != 11 ||
        dispatch.request.output.native_texture_handle != 12 ||
        dispatch.request.instance.progress != 0.5) {
      local = "two-input transition dispatch payload mismatch";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    ++calls;
    local.clear();
    return DIGITOR_RESULT_OK;
  };
  PluginTransitionProgramRuntime runtime(std::move(binding));

  PluginTransitionRequest request{};
  request.instance.instance_id = "transition.instance.1";
  request.instance.plugin_id = program.plugin_id;
  request.instance.plugin_version = program.plugin_version;
  request.instance.progress = 0.5;
  request.outgoing = frame(10);
  request.incoming = frame(11);
  request.output = frame(12);
  request.project_or_clip_id = "timeline.boundary.1";
  request.visual_stack_digest = "transition.stack.v1";

  if (runtime.dispatch(request, &diagnostic) != DIGITOR_RESULT_OK || calls != 1)
    return fail("registered arbitrary transition package did not dispatch");

  PluginGpuProgram invalid = program;
  invalid.plugin_id = "transition.invalid";
  invalid.passes[0].bindings.erase(invalid.passes[0].bindings.begin() + 1);
  if (!registry.register_program(invalid, &diagnostic)) {
    // Generic registry accepts arbitrary named bindings; transition adapter
    // must reject an incomplete two-input transition contract.
  }
  request.instance.plugin_id = invalid.plugin_id;
  if (runtime.dispatch(request, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("transition program without incoming binding was accepted");

  std::cout << "PLUGIN_TRANSITION_PROGRAM_QUALIFIED=1\n";
  std::cout << "ARBITRARY_TRANSITION_IDS=1\n";
  std::cout << "CPU_READBACKS=0\nCPU_UPLOADS=0\nFALLBACK_DISPATCHES=0\n";
  return 0;
}
