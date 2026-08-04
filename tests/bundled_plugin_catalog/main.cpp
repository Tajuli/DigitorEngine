#include "digitor/bundled_plugin_catalog.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const std::string& message) {
  std::cerr << "BUNDLED_PLUGIN_CATALOG=FAIL\nDIAGNOSTIC=" << message << '\n';
  return 1;
}

std::string hash64(char c) { return std::string(64, c); }

digitor::RemotePluginCatalogEntry entry(
    std::string id, digitor::RemotePluginKind kind, char hash_char) {
  digitor::RemotePluginCatalogEntry out{};
  out.id = std::move(id);
  out.display_name = out.id;
  out.version = "1.0.0";
  out.minimum_engine_version = "5.0.0";
  out.kind = kind;
  out.publisher_key_id = "digitor.builtin";
  out.signature = "qualified-signature";
  out.parameters.push_back({"amount", "Amount", 0.0, 1.0, 0.5, true});
  digitor::RemotePluginArtifact artifact{};
  artifact.backend = digitor::RemotePluginBackend::windows_d3d12;
  artifact.url = "bundle://effects/" + out.id + ".digitorfx";
  artifact.sha256 = hash64(hash_char);
  artifact.package_path = "packages/" + out.id + ".digitorfx";
  out.artifacts.push_back(std::move(artifact));
  return out;
}
}  // namespace

int main() {
  using namespace digitor;
  std::size_t downloads = 0;
  std::size_t registrations = 0;

  RemotePluginMarketplaceBindings bindings{};
  bindings.engine_version = "5.0.0";
  bindings.backend = RemotePluginBackend::windows_d3d12;
  bindings.verify_signature = [](std::string_view key, std::string_view payload,
                                 std::string_view signature,
                                 std::string& diagnostic) {
    const bool ok = key == "digitor.builtin" && !payload.empty() &&
                    signature == "qualified-signature";
    diagnostic = ok ? "" : "signature mismatch";
    return ok;
  };
  bindings.download = [&](std::string_view url, std::vector<std::byte>& bytes,
                          std::string& diagnostic) {
    if (url.rfind("bundle://", 0) != 0) {
      diagnostic = "non-bundle URL reached bundled transport";
      return false;
    }
    ++downloads;
    bytes.assign(4, std::byte{0x2a});
    diagnostic.clear();
    return true;
  };
  bindings.sha256 = [](const std::vector<std::byte>&) { return hash64('a'); };
  bindings.install_package = [](const RemotePluginCatalogEntry& plugin,
                                const RemotePluginArtifact&,
                                const std::vector<std::byte>&,
                                std::string& installed_path,
                                std::string& diagnostic) {
    installed_path = "/bundle/installed/" + plugin.id;
    diagnostic.clear();
    return true;
  };
  bindings.register_runtime = [&](const RemotePluginCatalogEntry&,
                                  std::string_view installed_path,
                                  std::string& diagnostic) {
    if (installed_path.empty()) {
      diagnostic = "installed path missing";
      return false;
    }
    ++registrations;
    diagnostic.clear();
    return true;
  };

  RemotePluginMarketplace marketplace(std::move(bindings));
  BundledPluginCatalogInstaller installer(marketplace);

  RemotePluginCatalog catalog{};
  catalog.schema_version = 2;
  catalog.catalog_id = "digitor.builtin.catalog";
  catalog.generated_at = "2026-08-04T00:00:00Z";
  catalog.plugins.push_back(entry("effect.box_blur", RemotePluginKind::effect, 'a'));
  catalog.plugins.push_back(entry("filter.cinematic", RemotePluginKind::filter, 'a'));

  auto result = installer.load_and_install(std::move(catalog));
  if (!result) return fail(result.diagnostic);
  if (result.installed.size() != 2 || downloads != 2 || registrations != 2)
    return fail("catalog entries were not installed generically");

  // Prove there is no ID table: an arbitrary valid future ID installs through
  // the same path without modifying engine source.
  RemotePluginCatalog future{};
  future.schema_version = 2;
  future.catalog_id = "digitor.future.catalog";
  future.generated_at = "2026-08-04T00:00:01Z";
  future.plugins.push_back(entry("effect.future_without_engine_edit",
                                 RemotePluginKind::effect, 'a'));
  auto future_result = installer.load_and_install(std::move(future));
  if (!future_result || future_result.installed.size() != 1)
    return fail("arbitrary future plugin ID required engine knowledge");

  std::cout << "BUNDLED_PLUGIN_CATALOG=PASS\n";
  return 0;
}
