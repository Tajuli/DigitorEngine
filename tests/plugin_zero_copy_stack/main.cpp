#include "digitor/plugin_zero_copy_stack.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_STACK_QUALIFICATION_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginGpuFrame frame(std::uint64_t handle) {
  digitor::PluginGpuFrame out{};
  out.backend = digitor::RemotePluginBackend::windows_d3d12;
  out.native_texture_handle = handle;
  out.synchronization_handle = 77;
  out.synchronization_value = handle;
  out.width = 1920;
  out.height = 1080;
  out.format = digitor::PluginPixelFormat::rgba16_float;
  out.primaries = digitor::PluginColorPrimaries::bt2020;
  out.transfer = digitor::PluginTransferFunction::pq;
  out.range = digitor::PluginColorRange::full;
  out.alpha = digitor::PluginAlphaMode::straight;
  out.timestamp_us = 1000;
  return out;
}

digitor::ConsumerPluginInstance instance(std::string id,
                                          std::string plugin) {
  digitor::ConsumerPluginInstance out{};
  out.instance_id = std::move(id);
  out.plugin_id = std::move(plugin);
  out.plugin_version = "1.0.0";
  out.kind = digitor::RemotePluginKind::effect;
  return out;
}
}  // namespace

int main() {
  using namespace digitor;
  std::vector<std::string> order;
  PluginZeroCopyBindings bindings{};
  bindings.selected_backend = RemotePluginBackend::windows_d3d12;
  bindings.dispatch = [&](const PluginZeroCopyRequest& request,
                          std::string& diagnostic) {
    order.push_back(request.instance.instance_id + ":" +
                    std::to_string(request.input.native_texture_handle) + "->" +
                    std::to_string(request.output.native_texture_handle));
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  };
  PluginZeroCopyFrameRuntime frame_runtime(std::move(bindings));
  PluginZeroCopyStackRuntime stack_runtime(
      frame_runtime, [](const ConsumerPluginInstance& value,
                        std::string& diagnostic) {
        const bool ok = value.plugin_version == "1.0.0";
        if (!ok) diagnostic = "version mismatch";
        return ok;
      });

  PluginZeroCopyStackRequest request{};
  request.surface = ConsumerPluginSurface::preview;
  request.project_or_clip_id = "clip.1";
  request.visual_stack_digest = "stack.filter.effect.v1";
  request.instances = {instance("filter.1", "filter.dynamic"),
                       instance("effect.1", "effect.dynamic")};
  request.source = frame(1);
  request.intermediates = {frame(2)};
  request.destination = frame(3);

  std::string diagnostic;
  if (stack_runtime.process(request, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("preview stack failed");
  if (order.size() != 2 || order[0] != "filter.1:1->2" ||
      order[1] != "effect.1:2->3")
    return fail("ordered native texture chain was not preserved");

  request.surface = ConsumerPluginSurface::export_frame;
  request.source = frame(11);
  request.intermediates = {frame(12)};
  request.destination = frame(13);
  if (stack_runtime.process(request, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("matching export stack failed parity");

  const auto stack = stack_runtime.telemetry();
  const auto per_pass = frame_runtime.telemetry();
  if (stack.stack_frames != 2 || stack.plugin_dispatches != 4 ||
      stack.preview_frames != 1 || stack.export_frames != 1 ||
      stack.cpu_readbacks != 0 || stack.cpu_uploads != 0 ||
      stack.cpu_fallback_frames != 0 || per_pass.parity_failures != 0)
    return fail("zero-copy stack telemetry mismatch");

  request.instances[1].plugin_version = "2.0.0";
  if (stack_runtime.process(request, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("unpinned plugin version was accepted");

  std::cout << "PLUGIN_ZERO_COPY_STACK=PASS\n";
  std::cout << "ORDERED_FILTER_EFFECT_CHAIN=PASS\n";
  std::cout << "PREVIEW_EXPORT_PER_PASS_PARITY=PASS\n";
  std::cout << "CPU_READBACKS=0\nCPU_UPLOADS=0\nCPU_FALLBACK_FRAMES=0\n";
  return 0;
}
