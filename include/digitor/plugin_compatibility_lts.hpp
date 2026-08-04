#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

struct PluginContractVersion {
  std::uint32_t major{1};
  std::uint32_t minor{0};
  std::uint32_t patch{0};
};

enum class PluginCompatibilityStatus : std::uint32_t {
  compatible,
  compatible_with_deprecation,
  migration_required,
  engine_too_old,
  plugin_too_old,
  unsupported_contract,
  invalid_contract
};

struct PluginCompatibilityRequest {
  PluginContractVersion engine_abi{};
  PluginContractVersion manifest_schema{};
  PluginContractVersion project_schema{};
  PluginContractVersion plugin_min_engine{};
  PluginContractVersion plugin_max_tested_engine{};
  bool uses_deprecated_fields{false};
  bool migration_available{false};
};

struct PluginCompatibilityResult {
  PluginCompatibilityStatus status{PluginCompatibilityStatus::invalid_contract};
  std::string diagnostic;
  bool may_preview{false};
  bool may_export_when_app_authorized{false};
  bool preserve_exact_plugin_identity{true};
};

struct PluginDeprecationRule {
  std::string field;
  PluginContractVersion deprecated_since{};
  PluginContractVersion removal_not_before{};
  std::string replacement;
};

class PluginCompatibilityLts {
 public:
  PluginCompatibilityResult evaluate(const PluginCompatibilityRequest& request) const;
  bool validate_deprecation_rules(const std::vector<PluginDeprecationRule>& rules,
                                  std::string& diagnostic) const;
  static bool version_less(const PluginContractVersion& lhs,
                           const PluginContractVersion& rhs) noexcept;
};

}  // namespace digitor
