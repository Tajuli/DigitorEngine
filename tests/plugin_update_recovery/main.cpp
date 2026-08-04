#include "digitor/plugin_update_recovery.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_UPDATE_RECOVERY_FAILED=" << message << '\n';
  return 1;
}
}

int main() {
  using namespace digitor;
  const std::string hash_v1(64, 'a');
  const std::string hash_v2(64, 'b');
  const std::string hash_alt(64, 'c');

  PluginProjectReference reference{};
  reference.instance_id = "plugin.instance.1";
  reference.plugin_id = "effect.cinematic.glow";
  reference.plugin_version = "1.0.0";
  reference.package_sha256 = hash_v1;
  reference.numeric_parameters = {{"strength", 0.7}, {"legacy_softness", 0.2}};

  PluginAvailablePackage exact{};
  exact.plugin_id = reference.plugin_id;
  exact.plugin_version = reference.plugin_version;
  exact.package_sha256 = hash_v1;
  exact.installed = true;

  auto decision = resolve_plugin_recovery(reference, {exact});
  if (decision.action != PluginRecoveryAction::use_exact_installed)
    return fail("exact installed package was not selected");

  exact.installed = false;
  exact.downloadable = true;
  decision = resolve_plugin_recovery(reference, {exact});
  if (decision.action != PluginRecoveryAction::download_exact_version)
    return fail("exact downloadable package was not selected");

  PluginAvailablePackage migrated{};
  migrated.plugin_id = reference.plugin_id;
  migrated.plugin_version = "2.0.0";
  migrated.package_sha256 = hash_v2;
  migrated.migrates_from_versions = {"1.0.0"};
  migrated.parameter_renames = {{"legacy_softness", "softness"}};
  migrated.installed = true;

  decision = resolve_plugin_recovery(reference, {migrated});
  if (decision.action != PluginRecoveryAction::migrate_to_compatible ||
      decision.migrated_numeric_parameters.count("softness") != 1 ||
      decision.migrated_numeric_parameters.count("legacy_softness") != 0)
    return fail("declared compatible migration was not applied");

  PluginAvailablePackage replacement{};
  replacement.plugin_id = "effect.safe.replacement";
  replacement.plugin_version = "1.0.0";
  replacement.package_sha256 = hash_alt;
  replacement.downloadable = true;
  decision = resolve_plugin_recovery(reference, {replacement}, replacement.plugin_id);
  if (decision.action != PluginRecoveryAction::replace_with_selected)
    return fail("app-selected replacement was not selected");

  replacement.revoked = true;
  decision = resolve_plugin_recovery(reference, {replacement}, replacement.plugin_id);
  if (decision.action != PluginRecoveryAction::disable_instance)
    return fail("unresolved plugin instance was not safely disabled");

  migrated.migrates_from_versions.clear();
  std::string diagnostic;
  if (evaluate_plugin_migration(reference, migrated, diagnostic) !=
      PluginMigrationCompatibility::incompatible)
    return fail("undeclared migration was accepted");

  std::cout << "PLUGIN_UPDATE_RECOVERY_QUALIFIED=1\n";
  std::cout << "EXACT_VERSION_PINNING_PRESERVED=1\n";
  std::cout << "SILENT_VERSION_SUBSTITUTION=0\n";
  std::cout << "MAIN_RENDERING_FEATURES_CHANGED=0\n";
  std::cout << "ENGINE_PROCESSING_POLICY_CHANGED=0\n";
  return 0;
}
