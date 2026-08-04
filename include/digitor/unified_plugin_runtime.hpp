#pragma once

#include "digitor/plugin_transition_runtime.hpp"
#include "digitor/plugin_zero_copy_frame.hpp"
#include "digitor/remote_plugin_marketplace.hpp"

#include <functional>
#include <string>
#include <variant>

namespace digitor {

enum class UnifiedPluginSurface : std::uint32_t {
  preview,
  export_frame
};

struct UnifiedPluginSingleInputRequest final {
  RemotePluginKind kind{RemotePluginKind::effect};
  UnifiedPluginSurface surface{UnifiedPluginSurface::preview};
  PluginZeroCopyRequest request;
};

struct UnifiedPluginTransitionRequest final {
  UnifiedPluginSurface surface{UnifiedPluginSurface::preview};
  PluginTransitionRequest request;
};

using UnifiedPluginRequest = std::variant<
    UnifiedPluginSingleInputRequest,
    UnifiedPluginTransitionRequest>;

using UnifiedPluginSingleInputDispatch = std::function<DigitorResult(
    const PluginZeroCopyRequest&, std::string* diagnostic)>;
using UnifiedPluginTransitionDispatch = std::function<DigitorResult(
    const PluginTransitionRequest&, std::string* diagnostic)>;

struct UnifiedPluginRuntimeBindings final {
  UnifiedPluginSingleInputDispatch dispatch_single_input;
  UnifiedPluginTransitionDispatch dispatch_transition;
};

class UnifiedPluginRuntime final {
 public:
  explicit UnifiedPluginRuntime(UnifiedPluginRuntimeBindings bindings);

  [[nodiscard]] DigitorResult dispatch(
      const UnifiedPluginRequest& request,
      std::string* diagnostic = nullptr) const noexcept;

 private:
  UnifiedPluginRuntimeBindings bindings_;
};

}  // namespace digitor
