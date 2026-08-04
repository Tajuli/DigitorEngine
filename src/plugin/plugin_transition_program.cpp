#include "digitor/plugin_transition_program.hpp"

#include <algorithm>
#include <utility>

namespace digitor {
namespace {

bool has_binding(const PluginGpuPassDescriptor& pass,
                 std::string_view name,
                 bool writable) noexcept {
  return std::any_of(pass.bindings.begin(), pass.bindings.end(),
                     [name, writable](const PluginGpuBinding& binding) {
                       return binding.name == name &&
                              binding.writable == writable;
                     });
}

}  // namespace

bool validate_transition_program_contract(
    const PluginGpuProgram& program,
    std::string& diagnostic) noexcept {
  if (program.passes.empty()) {
    diagnostic = "transition program has no GPU passes";
    return false;
  }
  for (const auto& pass : program.passes) {
    if (!has_binding(pass, "outgoing", false) ||
        !has_binding(pass, "incoming", false) ||
        !has_binding(pass, "output", true) ||
        !has_binding(pass, "progress", false)) {
      diagnostic =
          "transition pass requires outgoing, incoming, output and progress bindings";
      return false;
    }
    if (!pass.preserves_alpha || !pass.deterministic) {
      diagnostic = "transition pass violates alpha or determinism policy";
      return false;
    }
  }
  diagnostic.clear();
  return true;
}

PluginTransitionProgramRuntime::PluginTransitionProgramRuntime(
    PluginTransitionProgramBinding binding)
    : binding_(std::move(binding)) {}

DigitorResult PluginTransitionProgramRuntime::dispatch(
    const PluginTransitionRequest& request,
    std::string* diagnostic) const noexcept {
  std::string local;
  if (!validate_plugin_transition_request(request, binding_.selected_backend,
                                          local)) {
    if (diagnostic) *diagnostic = local;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (!binding_.registry) {
    if (diagnostic) *diagnostic = "transition program registry is unavailable";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  const auto program = binding_.registry->resolve(
      request.instance.plugin_id, request.instance.plugin_version,
      binding_.selected_backend, plugin_program_format(request.outgoing.format));
  if (!program) {
    if (diagnostic) *diagnostic =
        "compatible transition GPU program is not registered";
    return DIGITOR_RESULT_UNSUPPORTED;
  }
  if (!validate_transition_program_contract(*program, local)) {
    if (diagnostic) *diagnostic = local;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (!binding_.record) {
    if (diagnostic) *diagnostic = "transition GPU recorder is unavailable";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  PluginTransitionDispatch dispatch{};
  dispatch.request = request;
  const auto result = binding_.record(dispatch, local);
  if (result != DIGITOR_RESULT_OK) {
    if (diagnostic) *diagnostic = local.empty()
        ? "transition program recording failed without fallback" : local;
    return result;
  }
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

}  // namespace digitor
