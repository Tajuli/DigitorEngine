#include "digitor/plugin_install_store.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_INSTALL_STORE_FAILED=" << message << '\n';
  return 1;
}

digitor::RemotePluginInstallRecord record(
    std::string id, std::string version, char hash_char) {
  digitor::RemotePluginInstallRecord value{};
  value.id = std::move(id);
  value.version = std::move(version);
  value.package_path = "plugins/" + value.id + "/" + value.version;
  value.sha256 = std::string(64, hash_char);
  value.kind = digitor::RemotePluginKind::effect;
  value.state = digitor::RemotePluginInstallState::installed;
  return value;
}
}  // namespace

int main() {
  using namespace digitor;

  std::vector<RemotePluginInstallRecord> disk{
      record("effect.cinematic.glow", "1.2.0", 'a')};
  std::size_t saves = 0;
  std::size_t removals = 0;

  PluginInstallStoreBindings bindings{};
  bindings.load = [&](std::vector<RemotePluginInstallRecord>& out,
                      std::string& diagnostic) {
    out = disk;
    diagnostic.clear();
    return true;
  };
  bindings.save = [&](const RemotePluginInstallRecord& value,
                      std::string& diagnostic) {
    ++saves;
    bool replaced = false;
    for (auto& existing : disk) {
      if (existing.id == value.id) {
        existing = value;
        replaced = true;
      }
    }
    if (!replaced) disk.push_back(value);
    diagnostic.clear();
    return true;
  };
  bindings.remove = [&](std::string_view id, std::string& diagnostic) {
    ++removals;
    for (auto it = disk.begin(); it != disk.end(); ++it) {
      if (it->id == id) {
        disk.erase(it);
        diagnostic.clear();
        return true;
      }
    }
    diagnostic = "not found";
    return false;
  };

  PluginInstallStore store(bindings);
  std::string diagnostic;
  if (store.restore(&diagnostic) != DIGITOR_RESULT_OK ||
      store.records().size() != 1)
    return fail("installed plugin state did not restore");

  PluginProjectPin exact{"effect.cinematic.glow", "1.2.0",
                         std::string(64, 'a')};
  if (!store.resolve(exact))
    return fail("exact plugin version and hash pin did not resolve");

  PluginProjectPin wrong_version = exact;
  wrong_version.plugin_version = "1.3.0";
  if (store.resolve(wrong_version).status != PluginPinStatus::version_mismatch)
    return fail("version mismatch was not reported");

  PluginProjectPin wrong_hash = exact;
  wrong_hash.package_sha256 = std::string(64, 'b');
  if (store.resolve(wrong_hash).status != PluginPinStatus::hash_mismatch)
    return fail("hash mismatch was not reported");

  const auto transition = record("transition.prism.wipe", "2.0.1", 'c');
  if (store.persist(transition, &diagnostic) != DIGITOR_RESULT_OK || saves != 1)
    return fail("plugin installation was not persisted");

  PluginInstallStore restarted(bindings);
  if (restarted.restore(&diagnostic) != DIGITOR_RESULT_OK ||
      !restarted.installed(transition.id))
    return fail("persisted plugin did not survive restart");

  if (restarted.erase(transition.id, &diagnostic) != DIGITOR_RESULT_OK ||
      removals != 1 || restarted.installed(transition.id))
    return fail("plugin removal was not persisted");

  disk.push_back(disk.front());
  PluginInstallStore duplicate_store(bindings);
  if (duplicate_store.restore(&diagnostic) == DIGITOR_RESULT_OK)
    return fail("duplicate persistent plugin ids were accepted");

  std::cout << "PLUGIN_INSTALL_STORE_QUALIFIED=1\n";
  std::cout << "EXACT_VERSION_PINNING=1\n";
  std::cout << "PACKAGE_HASH_PINNING=1\n";
  std::cout << "RESTART_RESTORE=1\n";
  return 0;
}
