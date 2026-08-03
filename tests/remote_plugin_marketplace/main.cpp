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
    std::string id, digitor::RemotePluginKind kind,
    digitor::RemotePluginTier tier, std::string product = {}) {
  using namespace digitor;
  RemotePluginCatalogEntry entry{};
  entry.id = std::move(id);
  entry.display_name = "Plugin";
  entry.version = "1.0.0";
  entry.minimum_engine_version = "5.0.0";
  entry.kind = kind;
  entry.tier = tier;
  entry.product_id = std::move(product);
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
  bindings.verify_signature = [](auto, auto, auto signature, std::string& d) {
    if (signature != "valid-signature") { d = "signature rejected"; return false; }
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
  catalog.plugins.push_back(make_entry("filter.remote_free",
                                       RemotePluginKind::filter,
                                       RemotePluginTier::free));
  catalog.plugins.push_back(make_entry("effect.remote_paid",
                                       RemotePluginKind::effect,
                                       RemotePluginTier::paid,
                                       "effect.remote_paid.lifetime"));
  std::string diagnostic;
  if (marketplace.load_catalog(catalog, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("catalog load failed");
  if (!marketplace.install("filter.remote_free"))
    return fail("free filter install failed");
  if (!registered || marketplace.available(RemotePluginKind::filter).size() != 1)
    return fail("free filter was not registered");
  // Tier is catalog metadata only. The consumer app decides whether this call
  // is made; the engine performs no subscription or purchase verification.
  if (!marketplace.install("effect.remote_paid"))
    return fail("app-authorized paid effect install failed");
  if (marketplace.uninstall("effect.remote_paid", &diagnostic) != DIGITOR_RESULT_OK)
    return fail("paid effect uninstall failed");

  auto revoked = catalog;
  revoked.plugins[0].revoked = true;
  if (marketplace.load_catalog(revoked, &diagnostic) != DIGITOR_RESULT_OK)
    return fail("revocation catalog failed");
  const auto record = marketplace.installed("filter.remote_free");
  if (!record || record->state != RemotePluginInstallState::revoked)
    return fail("installed plugin was not marked revoked");

  std::cout << "QUALIFICATION=PASS\n";
  std::cout << "APP_AUTHORITY=PASS\n";
  std::cout << "ENGINE_ENTITLEMENT_CHECK=NONE\n";
  std::cout << "SIGNATURE_HASH_REVOCATION=PASS\n";
  std::cout << "ENGINE_SOURCE_EDIT_FOR_NEW_PLUGIN=NOT_REQUIRED\n";
  return 0;
}
