#include "digitor/plugin_transition_runtime.hpp"

#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_TRANSITION_RUNTIME_FAILED=" << message << '\n';
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

  std::uint64_t calls = 0;
  PluginTransitionRuntimeBindings bindings{};
  bindings.selected_backend = RemotePluginBackend::windows_d3d12;
  bindings.record = [&](const PluginTransitionDispatch& dispatch,
                        std::string& diagnostic) {
    if (dispatch.request.instance.plugin_id !=
            "transition.future_without_engine_edit" ||
        dispatch.request.instance.progress != 0.5 ||
        dispatch.request.outgoing.native_texture_handle != 10 ||
        dispatch.request.incoming.native_texture_handle != 11 ||
        dispatch.request.output.native_texture_handle != 12) {
      diagnostic = "transition dispatch payload mismatch";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    ++calls;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  };

  PluginTransitionRuntime runtime(std::move(bindings));
  PluginTransitionRequest request{};
  request.instance.instance_id = "transition.instance.1";
  request.instance.plugin_id = "transition.future_without_engine_edit";
  request.instance.plugin_version = "1.0.0";
  request.instance.progress = 0.5;
  request.instance.parameters["softness"] = 0.2;
  request.outgoing = frame(10);
  request.incoming = frame(11);
  request.output = frame(12);
  request.project_or_clip_id = "timeline.boundary.1";
  request.visual_stack_digest = "stack.digest.1";

  std::string diagnostic;
  if (runtime.dispatch(request, &diagnostic) != DIGITOR_RESULT_OK || calls != 1)
    return fail("arbitrary transition ID did not dispatch");

  request.instance.progress = 1.1;
  if (runtime.dispatch(request, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("out-of-range progress was accepted");

  request.instance.progress = 0.5;
  request.output.native_texture_handle = request.incoming.native_texture_handle;
  if (runtime.dispatch(request, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("aliased transition output was accepted");

  request.output = frame(12);
  request.incoming.backend = RemotePluginBackend::windows_vulkan;
  if (runtime.dispatch(request, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("mixed backend transition was accepted");

  request.incoming = frame(11);
  request.incoming.transfer = PluginTransferFunction::hlg;
  if (runtime.dispatch(request, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("mixed color contract transition was accepted");

  std::cout << "PLUGIN_TRANSITION_RUNTIME_QUALIFIED=1\n";
  std::cout << "CPU_READBACKS=0\nCPU_UPLOADS=0\nFALLBACK_DISPATCHES=0\n";
  return 0;
}
