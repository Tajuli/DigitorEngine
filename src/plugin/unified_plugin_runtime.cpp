#include "digitor/unified_plugin_runtime.hpp"

#include <utility>

namespace digitor {

UnifiedPluginRuntime::UnifiedPluginRuntime(UnifiedPluginRuntimeBindings bindings)
    : bindings_(std::move(bindings)) {}

DigitorResult UnifiedPluginRuntime::dispatch(
    const UnifiedPluginRequest& request,
    std::string* diagnostic) const noexcept {
  try {
    if (const auto* single =
            std::get_if<UnifiedPluginSingleInputRequest>(&request)) {
      if (single->kind == RemotePluginKind::transition) {
        if (diagnostic) {
          *diagnostic = "transition plugins require the two-input request contract";
        }
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      if (!bindings_.dispatch_single_input) {
        if (diagnostic) *diagnostic = "single-input plugin runtime is unavailable";
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      return bindings_.dispatch_single_input(single->request, diagnostic);
    }

    const auto* transition =
        std::get_if<UnifiedPluginTransitionRequest>(&request);
    if (!transition) {
      if (diagnostic) *diagnostic = "unrecognized unified plugin request";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    if (!bindings_.dispatch_transition) {
      if (diagnostic) *diagnostic = "transition plugin runtime is unavailable";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    return bindings_.dispatch_transition(transition->request, diagnostic);
  } catch (...) {
    if (diagnostic) *diagnostic = "unified plugin dispatch failed at the ABI boundary";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
}

}  // namespace digitor
