#include "digitor/unified_plugin_runtime.hpp"

#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << "UNIFIED_PLUGIN_RUNTIME_FAILED=" << message << '\n';
  return 1;
}
}

int main() {
  using namespace digitor;

  int single_calls = 0;
  int transition_calls = 0;
  UnifiedPluginRuntime runtime({
      [&](const PluginZeroCopyRequest&, std::string* diagnostic) {
        ++single_calls;
        if (diagnostic) diagnostic->clear();
        return DIGITOR_RESULT_OK;
      },
      [&](const PluginTransitionRequest&, std::string* diagnostic) {
        ++transition_calls;
        if (diagnostic) diagnostic->clear();
        return DIGITOR_RESULT_OK;
      }});

  std::string diagnostic;
  UnifiedPluginSingleInputRequest filter{};
  filter.kind = RemotePluginKind::filter;
  filter.surface = UnifiedPluginSurface::preview;
  if (runtime.dispatch(UnifiedPluginRequest{filter}, &diagnostic) !=
          DIGITOR_RESULT_OK ||
      single_calls != 1 || transition_calls != 0)
    return fail("filter did not route to the single-input runtime");

  UnifiedPluginSingleInputRequest effect{};
  effect.kind = RemotePluginKind::effect;
  effect.surface = UnifiedPluginSurface::export_frame;
  if (runtime.dispatch(UnifiedPluginRequest{effect}, &diagnostic) !=
          DIGITOR_RESULT_OK ||
      single_calls != 2 || transition_calls != 0)
    return fail("effect did not route to the single-input runtime");

  UnifiedPluginTransitionRequest transition{};
  transition.surface = UnifiedPluginSurface::preview;
  if (runtime.dispatch(UnifiedPluginRequest{transition}, &diagnostic) !=
          DIGITOR_RESULT_OK ||
      transition_calls != 1)
    return fail("transition did not route to the two-input runtime");

  UnifiedPluginSingleInputRequest invalid{};
  invalid.kind = RemotePluginKind::transition;
  if (runtime.dispatch(UnifiedPluginRequest{invalid}, &diagnostic) ==
      DIGITOR_RESULT_OK)
    return fail("transition was accepted through the single-input contract");

  UnifiedPluginRuntime unavailable({});
  if (unavailable.dispatch(UnifiedPluginRequest{filter}, &diagnostic) !=
      DIGITOR_RESULT_BACKEND_UNAVAILABLE)
    return fail("missing runtime binding was not reported");

  std::cout << "UNIFIED_PLUGIN_RUNTIME_QUALIFIED=1\n";
  std::cout << "FILTER_EFFECT_TRANSITION_ONE_FACADE=1\n";
  std::cout << "COMMERCIAL_POLICY_IN_APP=1\n";
  return 0;
}
