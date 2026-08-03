#include "digitor/plugin_zero_copy_frame.hpp"

#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_ZERO_COPY_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginZeroCopyRequest request_for(
    digitor::ConsumerPluginSurface surface,
    std::int64_t timestamp,
    std::string digest = "stack-v1") {
  using namespace digitor;
  PluginZeroCopyRequest request{};
  request.instance.instance_id = surface == ConsumerPluginSurface::preview
      ? "preview-instance" : "export-instance";
  request.instance.plugin_id = "effect.cinematic_glow";
  request.instance.plugin_version = "1.2.0";
  request.instance.kind = RemotePluginKind::effect;
  request.surface = surface;
  request.project_or_clip_id = "clip.1";
  request.visual_stack_digest = std::move(digest);
  request.input.backend = RemotePluginBackend::windows_d3d12;
  request.input.native_texture_handle = 100;
  request.input.synchronization_handle = 200;
  request.input.synchronization_value = 3;
  request.input.width = 3840;
  request.input.height = 2160;
  request.input.format = PluginPixelFormat::rgba16_float;
  request.input.primaries = PluginColorPrimaries::bt2020;
  request.input.transfer = PluginTransferFunction::pq;
  request.input.range = PluginColorRange::full;
  request.input.alpha = PluginAlphaMode::straight;
  request.input.timestamp_us = timestamp;
  request.output = request.input;
  request.output.native_texture_handle = 101;
  return request;
}
}  // namespace

int main() {
  using namespace digitor;
  std::uint64_t dispatches = 0;
  PluginZeroCopyBindings bindings{};
  bindings.selected_backend = RemotePluginBackend::windows_d3d12;
  bindings.dispatch = [&](const PluginZeroCopyRequest& request,
                          std::string& diagnostic) {
    if (request.input.native_texture_handle == 0 ||
        request.output.native_texture_handle == 0)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    ++dispatches;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  };
  PluginZeroCopyFrameRuntime runtime(std::move(bindings));
  std::string diagnostic;

  const auto preview = request_for(ConsumerPluginSurface::preview, 1000);
  if (runtime.process(preview, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("preview GPU frame failed");
  const auto export_frame = request_for(ConsumerPluginSurface::export_frame, 1000);
  if (runtime.process(export_frame, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("matching export GPU frame failed");

  auto mismatched = request_for(ConsumerPluginSurface::export_frame, 2000);
  if (runtime.process(request_for(ConsumerPluginSurface::preview, 2000),
                      &diagnostic) != DIGITOR_RESULT_OK)
    return fail("second preview failed");
  mismatched.visual_stack_digest = "different-stack";
  if (runtime.process(mismatched, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("preview/export stack mismatch was accepted");

  auto color_changed = request_for(ConsumerPluginSurface::preview, 3000);
  color_changed.output.transfer = PluginTransferFunction::srgb;
  if (runtime.process(color_changed, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("color encoding change was accepted");

  auto wrong_backend = request_for(ConsumerPluginSurface::preview, 4000);
  wrong_backend.output.backend = RemotePluginBackend::windows_vulkan;
  if (runtime.process(wrong_backend, &diagnostic) == DIGITOR_RESULT_OK)
    return fail("selected backend mismatch was accepted");

  const auto telemetry = runtime.telemetry();
  if (dispatches != 2 || telemetry.gpu_dispatches != 2 ||
      telemetry.preview_frames != 1 || telemetry.export_frames != 1 ||
      telemetry.cpu_readbacks != 0 || telemetry.cpu_uploads != 0 ||
      telemetry.cpu_fallback_frames != 0 || telemetry.parity_failures != 1)
    return fail("zero-copy telemetry contract failed");

  std::cout << "PLUGIN_ZERO_COPY_QUALIFICATION=PASS\n";
  std::cout << "GPU_FIRST=PASS\n";
  std::cout << "CPU_READBACKS=0\n";
  std::cout << "CPU_UPLOADS=0\n";
  std::cout << "CPU_FALLBACK_FRAMES=0\n";
  std::cout << "PREVIEW_EXPORT_STACK_IDENTITY=PASS\n";
  std::cout << "COLOR_ENCODING_PRESERVATION=PASS\n";
  return 0;
}
