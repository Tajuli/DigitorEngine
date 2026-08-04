#include "digitor/plugin_update_recovery.hpp"

#include <string>
#include <vector>

int main() {
  using namespace digitor;
  PluginProjectReference reference{};
  reference.instance_id = "clip.42.effect.3";
  reference.plugin_id = "effect.example";
  reference.plugin_version = "1.0.0";
  reference.package_sha256 = std::string(64, 'a');

  PluginAvailablePackage exact{};
  exact.plugin_id = reference.plugin_id;
  exact.plugin_version = reference.plugin_version;
  exact.package_sha256 = reference.package_sha256;
  exact.downloadable = true;

  const auto decision = resolve_plugin_recovery(reference, {exact});
  return decision.action == PluginRecoveryAction::download_exact_version ? 0 : 1;
}
