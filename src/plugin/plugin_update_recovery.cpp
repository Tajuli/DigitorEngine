#include "digitor/plugin_update_recovery.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace digitor {
namespace {

bool valid_id(std::string_view value, std::size_t limit) noexcept {
  if (value.empty() || value.size() > limit) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '_' || c == '-';
  });
}

bool valid_sha256(std::string_view value) noexcept {
  if (value.size() != 64) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isxdigit(c) != 0;
  });
}

bool same_exact_identity(const PluginProjectReference& reference,
                         const PluginAvailablePackage& package) noexcept {
  return package.plugin_id == reference.plugin_id &&
         package.plugin_version == reference.plugin_version &&
         package.package_sha256 == reference.package_sha256;
}

std::unordered_map<std::string, double> migrate_parameters(
    const PluginProjectReference& reference,
    const PluginAvailablePackage& package) {
  std::unordered_map<std::string, double> migrated;
  for (const auto& [id, value] : reference.numeric_parameters) {
    if (std::find(package.removed_parameters.begin(),
                  package.removed_parameters.end(), id) !=
        package.removed_parameters.end()) {
      continue;
    }
    const auto rename = package.parameter_renames.find(id);
    migrated[rename == package.parameter_renames.end() ? id : rename->second] = value;
  }
  return migrated;
}

}  // namespace

bool validate_plugin_project_reference(const PluginProjectReference& reference,
                                       std::string& diagnostic) noexcept {
  if (!valid_id(reference.instance_id, 160) ||
      !valid_id(reference.plugin_id, 200) ||
      !valid_id(reference.plugin_version, 80) ||
      !valid_sha256(reference.package_sha256) ||
      reference.numeric_parameters.size() > 256) {
    diagnostic = "project plugin reference is invalid";
    return false;
  }
  for (const auto& [id, value] : reference.numeric_parameters) {
    if (!valid_id(id, 160) || !std::isfinite(value)) {
      diagnostic = "project plugin parameter is invalid";
      return false;
    }
  }
  diagnostic.clear();
  return true;
}

PluginMigrationCompatibility evaluate_plugin_migration(
    const PluginProjectReference& reference,
    const PluginAvailablePackage& candidate,
    std::string& diagnostic) noexcept {
  if (!validate_plugin_project_reference(reference, diagnostic) ||
      !valid_id(candidate.plugin_id, 200) ||
      !valid_id(candidate.plugin_version, 80) ||
      !valid_sha256(candidate.package_sha256) || candidate.revoked) {
    diagnostic = "plugin migration candidate is invalid or revoked";
    return PluginMigrationCompatibility::invalid;
  }
  if (same_exact_identity(reference, candidate)) {
    diagnostic.clear();
    return PluginMigrationCompatibility::exact;
  }
  if (candidate.plugin_id != reference.plugin_id ||
      std::find(candidate.migrates_from_versions.begin(),
                candidate.migrates_from_versions.end(),
                reference.plugin_version) ==
          candidate.migrates_from_versions.end()) {
    diagnostic = "plugin candidate does not declare a compatible migration";
    return PluginMigrationCompatibility::incompatible;
  }
  std::set<std::string> destinations;
  for (const auto& [source, destination] : candidate.parameter_renames) {
    if (!valid_id(source, 160) || !valid_id(destination, 160) ||
        !destinations.insert(destination).second) {
      diagnostic = "plugin parameter migration map is invalid";
      return PluginMigrationCompatibility::invalid;
    }
  }
  diagnostic.clear();
  return PluginMigrationCompatibility::compatible;
}

PluginRecoveryDecision resolve_plugin_recovery(
    const PluginProjectReference& reference,
    const std::vector<PluginAvailablePackage>& packages,
    std::string_view app_selected_replacement_id) noexcept {
  PluginRecoveryDecision decision{};
  std::string diagnostic;
  if (!validate_plugin_project_reference(reference, diagnostic)) {
    decision.diagnostic = diagnostic;
    return decision;
  }

  for (const auto& package : packages) {
    if (same_exact_identity(reference, package) && !package.revoked) {
      decision.action = package.installed
          ? PluginRecoveryAction::use_exact_installed
          : (package.downloadable
                 ? PluginRecoveryAction::download_exact_version
                 : PluginRecoveryAction::unresolved);
      decision.selected_version = package.plugin_version;
      decision.selected_sha256 = package.package_sha256;
      decision.migrated_numeric_parameters = reference.numeric_parameters;
      decision.diagnostic = package.installed
          ? "exact pinned plugin is installed"
          : (package.downloadable ? "exact pinned plugin can be downloaded"
                                  : "exact pinned plugin is unavailable");
      return decision;
    }
  }

  for (const auto& package : packages) {
    if (evaluate_plugin_migration(reference, package, diagnostic) ==
            PluginMigrationCompatibility::compatible &&
        (package.installed || package.downloadable)) {
      decision.action = PluginRecoveryAction::migrate_to_compatible;
      decision.selected_version = package.plugin_version;
      decision.selected_sha256 = package.package_sha256;
      decision.migrated_numeric_parameters = migrate_parameters(reference, package);
      decision.diagnostic = "compatible plugin migration is available";
      return decision;
    }
  }

  if (!app_selected_replacement_id.empty()) {
    const auto replacement = std::find_if(
        packages.begin(), packages.end(), [&](const auto& package) {
          return package.plugin_id == app_selected_replacement_id &&
                 !package.revoked && (package.installed || package.downloadable);
        });
    if (replacement != packages.end()) {
      decision.action = PluginRecoveryAction::replace_with_selected;
      decision.selected_version = replacement->plugin_version;
      decision.selected_sha256 = replacement->package_sha256;
      decision.diagnostic = "app-selected replacement is available";
      return decision;
    }
  }

  decision.action = PluginRecoveryAction::disable_instance;
  decision.diagnostic =
      "plugin instance must remain disabled until the app resolves it";
  return decision;
}

}  // namespace digitor
