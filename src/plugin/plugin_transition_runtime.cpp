#include "digitor/plugin_transition_runtime.hpp"

#include <cmath>
#include <utility>

namespace digitor {
namespace {

bool valid_frame(const PluginGpuFrame& frame) noexcept {
  return frame.native_texture_handle != 0 && frame.width != 0 &&
         frame.height != 0;
}

bool same_surface_contract(const PluginGpuFrame& a,
                           const PluginGpuFrame& b) noexcept {
  return a.backend == b.backend && a.width == b.width &&
         a.height == b.height && a.format == b.format &&
         a.primaries == b.primaries && a.transfer == b.transfer &&
         a.range == b.range && a.alpha == b.alpha;
}

}  // namespace

bool validate_plugin_transition_request(
    const PluginTransitionRequest& request,
    RemotePluginBackend selected_backend,
    std::string& diagnostic) noexcept {
  const auto& instance = request.instance;
  if (instance.instance_id.empty() || instance.plugin_id.empty() ||
      instance.plugin_version.empty() || request.project_or_clip_id.empty() ||
      request.visual_stack_digest.empty()) {
    diagnostic = "transition identity metadata is incomplete";
    return false;
  }
  if (!std::isfinite(instance.progress) || instance.progress < 0.0 ||
      instance.progress > 1.0) {
    diagnostic = "transition progress must be finite and normalized";
    return false;
  }
  if (!valid_frame(request.outgoing) || !valid_frame(request.incoming) ||
      !valid_frame(request.output)) {
    diagnostic = "transition requires three valid native GPU textures";
    return false;
  }
  if (request.outgoing.backend != selected_backend ||
      request.incoming.backend != selected_backend ||
      request.output.backend != selected_backend) {
    diagnostic = "transition request differs from selected backend";
    return false;
  }
  if (!same_surface_contract(request.outgoing, request.incoming) ||
      !same_surface_contract(request.outgoing, request.output)) {
    diagnostic =
        "transition surfaces differ in dimensions, format or color contract";
    return false;
  }
  if (request.output.native_texture_handle ==
          request.outgoing.native_texture_handle ||
      request.output.native_texture_handle ==
          request.incoming.native_texture_handle) {
    diagnostic = "transition output must not alias either input texture";
    return false;
  }
  if (instance.parameters.size() > 128) {
    diagnostic = "transition parameter count exceeds runtime limit";
    return false;
  }
  for (const auto& [id, value] : instance.parameters) {
    if (id.empty() || !std::isfinite(value)) {
      diagnostic = "transition parameter is invalid";
      return false;
    }
  }
  diagnostic.clear();
  return true;
}

PluginTransitionRuntime::PluginTransitionRuntime(
    PluginTransitionRuntimeBindings bindings)
    : bindings_(std::move(bindings)) {}

DigitorResult PluginTransitionRuntime::dispatch(
    const PluginTransitionRequest& request,
    std::string* diagnostic) const noexcept {
  std::string local;
  if (!validate_plugin_transition_request(request, bindings_.selected_backend,
                                          local)) {
    if (diagnostic) *diagnostic = local;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (!bindings_.record) {
    if (diagnostic) *diagnostic = "transition GPU recorder is unavailable";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  PluginTransitionDispatch dispatch{};
  dispatch.request = request;
  const auto result = bindings_.record(dispatch, local);
  if (result != DIGITOR_RESULT_OK) {
    if (diagnostic) {
      *diagnostic = local.empty()
          ? "transition GPU recording failed without fallback"
          : local;
    }
    return result;
  }
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

}  // namespace digitor
