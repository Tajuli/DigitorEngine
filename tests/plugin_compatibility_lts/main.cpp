#include "digitor/plugin_compatibility_lts.hpp"

#include <iostream>

int main() {
  using namespace digitor;
  PluginCompatibilityLts lts;

  PluginCompatibilityRequest compatible;
  compatible.engine_abi = {1, 4, 0};
  compatible.manifest_schema = {1, 3, 0};
  compatible.project_schema = {1, 2, 0};
  compatible.plugin_min_engine = {1, 1, 0};
  compatible.plugin_max_tested_engine = {1, 5, 0};
  const auto ok = lts.evaluate(compatible);
  if (ok.status != PluginCompatibilityStatus::compatible || !ok.may_preview ||
      !ok.may_export_when_app_authorized ||
      !ok.preserve_exact_plugin_identity) return 1;

  auto deprecated = compatible;
  deprecated.uses_deprecated_fields = true;
  if (lts.evaluate(deprecated).status !=
      PluginCompatibilityStatus::compatible_with_deprecation) return 2;

  auto engine_old = compatible;
  engine_old.plugin_min_engine = {1, 6, 0};
  if (lts.evaluate(engine_old).status !=
      PluginCompatibilityStatus::engine_too_old) return 3;

  auto migration = compatible;
  migration.manifest_schema = {1, 6, 0};
  migration.migration_available = true;
  if (lts.evaluate(migration).status !=
      PluginCompatibilityStatus::migration_required) return 4;

  auto incompatible_major = compatible;
  incompatible_major.manifest_schema = {2, 0, 0};
  if (lts.evaluate(incompatible_major).status !=
      PluginCompatibilityStatus::unsupported_contract) return 5;

  std::string diagnostic;
  std::vector<PluginDeprecationRule> rules = {
      {"legacy_strength", {1, 2, 0}, {1, 6, 0}, "strength"},
      {"legacy_mix", {1, 3, 0}, {1, 7, 0}, "mix"}};
  if (!lts.validate_deprecation_rules(rules, diagnostic)) return 6;
  rules.push_back(rules.front());
  if (lts.validate_deprecation_rules(rules, diagnostic)) return 7;

  std::cout
      << "PLUGIN_ABI_COMPATIBILITY_LTS=1\n"
      << "OLD_PLUGIN_SUPPORT=1\n"
      << "MANIFEST_VERSION_NEGOTIATION=1\n"
      << "PROJECT_SCHEMA_MIGRATION_GUARD=1\n"
      << "EXACT_PLUGIN_IDENTITY_PRESERVED=1\n"
      << "APP_OWNS_COMMERCIAL_POLICY=1\n"
      << "MAIN_RENDERING_FEATURES_CHANGED=0\n";
  return 0;
}
