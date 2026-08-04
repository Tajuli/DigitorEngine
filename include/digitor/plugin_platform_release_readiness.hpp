#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class PluginReleaseSeverity : std::uint32_t {
  info = 0,
  warning = 1,
  error = 2
};

struct PluginReleaseComponent {
  std::string name;
  std::string version;
  bool public_header_installed{false};
  bool runtime_linked{false};
  bool cross_platform_qualified{false};
};

struct PluginReleasePlatform {
  std::string name;
  bool catalog{false};
  bool installation{false};
  bool preview{false};
  bool export_processing{false};
  bool transition_processing{false};
};

struct PluginReleasePolicy {
  bool app_owns_commercial_state{true};
  bool paid_preview_for_free_user{true};
  bool paid_export_requires_app_authorization{true};
  bool engine_executes_commercial_policy{false};
};

struct PluginReleaseFinding {
  PluginReleaseSeverity severity{PluginReleaseSeverity::info};
  std::string code;
  std::string message;
};

struct PluginReleaseReadinessInput {
  std::string engine_version;
  std::vector<PluginReleaseComponent> components;
  std::vector<PluginReleasePlatform> platforms;
  PluginReleasePolicy policy{};
  bool filter_code_free{false};
  bool effect_code_free{false};
  bool transition_code_free{false};
  bool exact_version_pinning{false};
  bool signed_atomic_installation{false};
  bool preview_export_parity{false};
  bool main_rendering_features_changed{false};
};

struct PluginReleaseReadinessReport {
  bool ready{false};
  std::vector<PluginReleaseFinding> findings;
  std::string canonical_summary;
};

PluginReleaseReadinessReport evaluate_plugin_platform_release_readiness(
    const PluginReleaseReadinessInput& input);

}  // namespace digitor
