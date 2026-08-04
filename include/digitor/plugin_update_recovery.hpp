#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class PluginRecoveryAction : std::uint32_t {
  use_exact_installed,
  download_exact_version,
  migrate_to_compatible,
  replace_with_selected,
  disable_instance,
  unresolved
};

enum class PluginMigrationCompatibility : std::uint32_t {
  exact,
  compatible,
  incompatible,
  invalid
};

struct PluginProjectReference final {
  std::string instance_id;
  std::string plugin_id;
  std::string plugin_version;
  std::string package_sha256;
  std::unordered_map<std::string, double> numeric_parameters;
};

struct PluginAvailablePackage final {
  std::string plugin_id;
  std::string plugin_version;
  std::string package_sha256;
  std::vector<std::string> migrates_from_versions;
  std::unordered_map<std::string, std::string> parameter_renames;
  std::vector<std::string> removed_parameters;
  bool installed{};
  bool downloadable{};
  bool revoked{};
};

struct PluginRecoveryDecision final {
  PluginRecoveryAction action{PluginRecoveryAction::unresolved};
  std::string selected_version;
  std::string selected_sha256;
  std::unordered_map<std::string, double> migrated_numeric_parameters;
  std::string diagnostic;
};

[[nodiscard]] bool validate_plugin_project_reference(
    const PluginProjectReference& reference,
    std::string& diagnostic) noexcept;

[[nodiscard]] PluginMigrationCompatibility evaluate_plugin_migration(
    const PluginProjectReference& reference,
    const PluginAvailablePackage& candidate,
    std::string& diagnostic) noexcept;

[[nodiscard]] PluginRecoveryDecision resolve_plugin_recovery(
    const PluginProjectReference& reference,
    const std::vector<PluginAvailablePackage>& packages,
    std::string_view app_selected_replacement_id = {}) noexcept;

}  // namespace digitor
