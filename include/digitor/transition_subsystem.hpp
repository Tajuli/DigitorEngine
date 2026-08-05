#pragma once

#include "digitor/plugin_transition_runtime.hpp"
#include "digitor/production_transitions.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

enum class TransitionProviderKind : std::uint32_t {
  built_in = 0,
  plugin = 1,
};

struct TransitionDescriptor final {
  std::string id;
  std::string display_name;
  std::string version;
  TransitionProviderKind provider{TransitionProviderKind::built_in};
};

using BuiltInTransitionRecord = std::function<DigitorResult(
    const PluginTransitionRequest&,
    const TransitionSettings&,
    std::string& diagnostic)>;

struct TransitionSubsystemBindings final {
  RemotePluginBackend selected_backend{RemotePluginBackend::windows_d3d12};
  BuiltInTransitionRecord record_built_in;
  PluginTransitionRecord record_plugin;
};

class TransitionSubsystem final {
 public:
  explicit TransitionSubsystem(TransitionSubsystemBindings bindings);

  [[nodiscard]] bool register_plugin(TransitionDescriptor descriptor,
                                     std::string* diagnostic = nullptr);
  [[nodiscard]] const std::vector<TransitionDescriptor>& descriptors() const noexcept;
  [[nodiscard]] const TransitionDescriptor* find(std::string_view id) const noexcept;

  [[nodiscard]] TransitionResult render_reference(
      std::string_view transition_id,
      const TransitionFrame& outgoing,
      const TransitionFrame& incoming,
      TransitionFrame& output,
      const TransitionSettings& settings) const;

  [[nodiscard]] DigitorResult dispatch(
      const PluginTransitionRequest& request,
      std::string* diagnostic = nullptr) const noexcept;

 private:
  TransitionSubsystemBindings bindings_;
  std::vector<TransitionDescriptor> descriptors_;
};

[[nodiscard]] bool is_builtin_transition_id(std::string_view id) noexcept;
[[nodiscard]] TransitionSettings builtin_transition_settings(
    const PluginTransitionInstance& instance,
    std::string* diagnostic = nullptr) noexcept;

}  // namespace digitor

extern "C" {
std::uint32_t digitor_builtin_transition_count(void);
std::uint32_t digitor_builtin_transition_id(std::uint32_t index,
                                            char* output,
                                            std::uint32_t capacity);
}
