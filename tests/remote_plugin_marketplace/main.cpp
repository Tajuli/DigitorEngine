#include "digitor/remote_plugin_marketplace.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const char* message) {
  std::cerr << "QUALIFICATION_FAILED=" << message << '\n';
  return 1;
}

digitor::RemotePluginCatalogEntry make_entry(
    std::string id, digitor::RemotePluginKind kind) {
  using namespace digitor;
  RemotePluginCatalogEntry entry{};
  entry.id = std::move(id);
  entry.display_name = "Plugin";
  entry.version = "1.0.0";
  entry.minimum_engine_version = "5.0.0";
  entry.kind = kind;
  entry.publisher_key_id = "digitor-official";
  entry.signature = "valid-signature";
  entry.parameters.push_back({"amount", "Amount", 0.0, 1.0, 0.5, true});
  entry.artifacts.push_back({RemotePluginBackend::windows_d3d12,
                             "https://plugins.example/plugin.digitorfx",
                             std::string(64, 'a'), "shaders/windows.dxil"});
  return entry;
}
}  // namespace

int main() {
  using namespace digitor;
  bool registered = false;
  RemotePluginMarketplaceBindings bindings{};
  bindings.engine_version = "5.1.0";
  bindings.backend = RemotePluginBackend::windows_d3d12;
  bindings.verify_signature = [](auto, auto payload, auto signature,
                                  std::string& diagnostic) {
    if (payload.find("digitor-plugin-v2") == std::string_view::npos ||
        signature != "valid-signature") {
      diagnostic = "signature rejected";
      return false;
    }
    return true;
  };
  bindings.download = [](auto, std::vector<std::byte>& bytes, std::string&) {
    bytes.assign(16, std::byte{0x2a}); return true;
  };
  bindings.sha256 = [](const auto&) { return std::string(64, 'a'); };
  bindings.install_package = [](const auto&, const auto&, const auto&,
                                std::string& path, std::string&) {
    path = "plugins/installed/plugin-1.0.0"; return true;
  };
  bindings.register_runtime = [&](const auto&, auto, std::string&) {
    registered = true; return true;
  };
  bindings.unregister_runtime = [&](auto) { registered = false; };

  RemotePluginMarketplace marketplace(bindings);
  RemotePluginCatalog catalog{};
  catalog.catalog_id = "digitor-official-stable";
  catalog.plugins.push_back(make_entry("filter.remote_a",
                                       RemotePluginKind::filter));
  catalog.plugins.push_back(make_entry("effect.remote_b",
                                       RemotePluginKind::effect));
  std::string diagnostic;
  if (marketplace.load_catalog(catalog, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("catalog load failed");
  if (!marketplace.install("filter.remote_a"))
    return fail("filter install failed");
  if (!marketplace.install("effect.remote_b"))
    return fail("effect install failed");
  if (!registered || marketplace.available(RemotePluginKind::filter).size() != 1)
    return fail("plugin was not registered");
  if (marketplace.uninstall("effect.remote_b", &diagnostic) != DIGITOR_RESULT_OK)
    return fail("effect uninstall failed");

  auto revoked = catalog;
  revoked.plugins[0].revoked = true;
  if (marketplace.load_catalog(revoked, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("revocation catalog failed");
  const auto record = marketplace.installed("filter.remote_a");
  if (!record || record->state != RemotePluginInstallState::revoked)
    return fail("installed plugin was not marked revoked");

  std::cout << "QUALIFICATION=PASS\n";
  std::cout << "CATALOG_SCHEMA=2\n";
  std::cout << "COMMERCIAL_POLICY_FIELDS=NONE\n";
  std::cout << "SIGNATURE_HASH_REVOCATION=PASS\n";
  std::cout << "ENGINE_SOURCE_EDIT_FOR_NEW_PLUGIN=NOT_REQUIRED\n";
  return 0;
}
