#include "digitor/plugin_platform_release_readiness.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace digitor {
namespace {
void add(PluginReleaseReadinessReport& report,
         PluginReleaseSeverity severity,
         const char* code,
         std::string message) {
  report.findings.push_back({severity, code, std::move(message)});
}

bool has_error(const PluginReleaseReadinessReport& report) {
  return std::any_of(report.findings.begin(), report.findings.end(),
                     [](const auto& finding) {
                       return finding.severity == PluginReleaseSeverity::error;
                     });
}
}  // namespace

PluginReleaseReadinessReport evaluate_plugin_platform_release_readiness(
    const PluginReleaseReadinessInput& input) {
  PluginReleaseReadinessReport report;
  if (input.engine_version.empty()) {
    add(report, PluginReleaseSeverity::error, "engine_version_missing",
        "Engine version is required for a release record.");
  }

  std::set<std::string> component_names;
  for (const auto& component : input.components) {
    if (component.name.empty() || component.version.empty()) {
      add(report, PluginReleaseSeverity::error, "component_identity_invalid",
          "Every release component requires a name and version.");
      continue;
    }
    if (!component_names.insert(component.name).second) {
      add(report, PluginReleaseSeverity::error, "component_duplicate",
          "Duplicate release component: " + component.name);
    }
    if (!component.public_header_installed || !component.runtime_linked ||
        !component.cross_platform_qualified) {
      add(report, PluginReleaseSeverity::error, "component_incomplete",
          "Component is not fully installed, linked and qualified: " +
              component.name);
    }
  }
  if (input.components.empty()) {
    add(report, PluginReleaseSeverity::error, "components_missing",
        "No plugin-platform release components were supplied.");
  }

  const std::set<std::string> required_platforms = {
      "windows", "android", "macos", "ios"};
  std::set<std::string> seen_platforms;
  for (const auto& platform : input.platforms) {
    if (!seen_platforms.insert(platform.name).second) {
      add(report, PluginReleaseSeverity::error, "platform_duplicate",
          "Duplicate platform qualification: " + platform.name);
    }
    if (!platform.catalog || !platform.installation || !platform.preview ||
        !platform.export_processing || !platform.transition_processing) {
      add(report, PluginReleaseSeverity::error, "platform_incomplete",
          "Plugin lifecycle is incomplete on platform: " + platform.name);
    }
  }
  for (const auto& required : required_platforms) {
    if (!seen_platforms.count(required)) {
      add(report, PluginReleaseSeverity::error, "platform_missing",
          "Missing required platform qualification: " + required);
    }
  }

  if (!input.filter_code_free || !input.effect_code_free ||
      !input.transition_code_free) {
    add(report, PluginReleaseSeverity::error, "code_free_contract_incomplete",
        "Filter, effect and transition packages must all remain code-free.");
  }
  if (!input.exact_version_pinning) {
    add(report, PluginReleaseSeverity::error, "version_pinning_missing",
        "Exact plugin ID, version and SHA-256 pinning is required.");
  }
  if (!input.signed_atomic_installation) {
    add(report, PluginReleaseSeverity::error, "secure_install_missing",
        "Signed and atomic plugin installation is required.");
  }
  if (!input.preview_export_parity) {
    add(report, PluginReleaseSeverity::error, "parity_missing",
        "Preview/export plugin parity qualification is required.");
  }

  const auto& policy = input.policy;
  if (!policy.app_owns_commercial_state ||
      !policy.paid_preview_for_free_user ||
      !policy.paid_export_requires_app_authorization ||
      policy.engine_executes_commercial_policy) {
    add(report, PluginReleaseSeverity::error, "commercial_boundary_invalid",
        "Commercial state must remain app-owned; paid preview is allowed and "
        "paid export requires app authorization.");
  }
  if (input.main_rendering_features_changed) {
    add(report, PluginReleaseSeverity::error, "main_renderer_changed",
        "Release hardening must not change established rendering features.");
  }

  std::sort(report.findings.begin(), report.findings.end(),
            [](const auto& a, const auto& b) {
              if (a.code != b.code) return a.code < b.code;
              return a.message < b.message;
            });
  report.ready = !has_error(report);

  std::ostringstream summary;
  summary << "engine=" << input.engine_version << '\n';
  summary << "ready=" << (report.ready ? 1 : 0) << '\n';
  summary << "components=" << input.components.size() << '\n';
  summary << "platforms=" << input.platforms.size() << '\n';
  summary << "findings=" << report.findings.size() << '\n';
  summary << "commercial_policy_in_engine="
          << (policy.engine_executes_commercial_policy ? 1 : 0) << '\n';
  summary << "main_rendering_features_changed="
          << (input.main_rendering_features_changed ? 1 : 0) << '\n';
  report.canonical_summary = summary.str();
  return report;
}

}  // namespace digitor
