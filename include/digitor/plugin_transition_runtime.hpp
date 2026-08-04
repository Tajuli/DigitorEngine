#pragma once

#include "digitor/plugin_zero_copy_frame.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace digitor {

struct PluginTransitionInstance final {
  std::string instance_id;
  std::string plugin_id;
  std::string plugin_version;
  std::unordered_map<std::string, double> parameters;
  double progress{};
};

struct PluginTransitionRequest final {
  PluginTransitionInstance instance;
  PluginGpuFrame outgoing;
  PluginGpuFrame incoming;
  PluginGpuFrame output;
  std::string project_or_clip_id;
  std::string visual_stack_digest;
};

struct PluginTransitionDispatch final {
  PluginTransitionRequest request;
};

using PluginTransitionRecord = std::function<DigitorResult(
    const PluginTransitionDispatch&, std::string& diagnostic)>;

struct PluginTransitionRuntimeBindings final {
  RemotePluginBackend selected_backend{RemotePluginBackend::windows_d3d12};
  PluginTransitionRecord record;
};

class PluginTransitionRuntime final {
 public:
  explicit PluginTransitionRuntime(PluginTransitionRuntimeBindings bindings);

  [[nodiscard]] DigitorResult dispatch(
      const PluginTransitionRequest& request,
      std::string* diagnostic = nullptr) const noexcept;

 private:
  PluginTransitionRuntimeBindings bindings_;
};

[[nodiscard]] bool validate_plugin_transition_request(
    const PluginTransitionRequest& request,
    RemotePluginBackend selected_backend,
    std::string& diagnostic) noexcept;

}  // namespace digitor
