#include "digitor/plugin_compatibility_lts.hpp"

#include <algorithm>
#include <set>
#include <tuple>

namespace digitor {
namespace {

bool valid(const PluginContractVersion& version) noexcept {
  return version.major > 0;
}

bool equal(const PluginContractVersion& lhs,
           const PluginContractVersion& rhs) noexcept {
  return std::tie(lhs.major, lhs.minor, lhs.patch) ==
         std::tie(rhs.major, rhs.minor, rhs.patch);
}

}  // namespace

bool PluginCompatibilityLts::version_less(
    const PluginContractVersion& lhs,
    const PluginContractVersion& rhs) noexcept {
  return std::tie(lhs.major, lhs.minor, lhs.patch) <
         std::tie(rhs.major, rhs.minor, rhs.patch);
}

PluginCompatibilityResult PluginCompatibilityLts::evaluate(
    const PluginCompatibilityRequest& request) const {
  PluginCompatibilityResult result;
  if (!valid(request.engine_abi) || !valid(request.manifest_schema) ||
      !valid(request.project_schema) || !valid(request.plugin_min_engine) ||
      !valid(request.plugin_max_tested_engine)) {
    result.diagnostic = "invalid zero-major compatibility contract";
    return result;
  }

  if (request.engine_abi.major != request.manifest_schema.major ||
      request.engine_abi.major != request.project_schema.major) {
    result.status = PluginCompatibilityStatus::unsupported_contract;
    result.diagnostic = "major ABI, manifest and project contracts differ";
    return result;
  }

  if (version_less(request.engine_abi, request.plugin_min_engine)) {
    result.status = PluginCompatibilityStatus::engine_too_old;
    result.diagnostic = "plugin requires a newer DigitorEngine contract";
    return result;
  }

  if (request.plugin_min_engine.major != request.engine_abi.major) {
    result.status = PluginCompatibilityStatus::plugin_too_old;
    result.diagnostic = "plugin targets an unsupported ABI generation";
    return result;
  }

  if (request.manifest_schema.minor > request.engine_abi.minor ||
      request.project_schema.minor > request.engine_abi.minor) {
    result.status = request.migration_available
                        ? PluginCompatibilityStatus::migration_required
                        : PluginCompatibilityStatus::unsupported_contract;
    result.diagnostic = request.migration_available
                            ? "schema migration must run before activation"
                            : "newer schema has no declared migration";
    return result;
  }

  result.may_preview = true;
  result.may_export_when_app_authorized = true;
  if (request.uses_deprecated_fields) {
    result.status = PluginCompatibilityStatus::compatible_with_deprecation;
    result.diagnostic = "compatible, but deprecated fields should be migrated";
  } else {
    result.status = PluginCompatibilityStatus::compatible;
    result.diagnostic = version_less(request.plugin_max_tested_engine,
                                     request.engine_abi)
                            ? "compatible beyond publisher tested range"
                            : "compatible";
  }
  return result;
}

bool PluginCompatibilityLts::validate_deprecation_rules(
    const std::vector<PluginDeprecationRule>& rules,
    std::string& diagnostic) const {
  std::set<std::string> fields;
  for (const auto& rule : rules) {
    if (rule.field.empty() || rule.replacement.empty() ||
        !valid(rule.deprecated_since) || !valid(rule.removal_not_before) ||
        !version_less(rule.deprecated_since, rule.removal_not_before)) {
      diagnostic = "invalid plugin deprecation rule";
      return false;
    }
    if (!fields.insert(rule.field).second) {
      diagnostic = "duplicate plugin deprecation rule";
      return false;
    }
    if (rule.deprecated_since.major != rule.removal_not_before.major) {
      diagnostic = "deprecation removal must remain within the ABI generation";
      return false;
    }
  }
  diagnostic.clear();
  return true;
}

}  // namespace digitor
