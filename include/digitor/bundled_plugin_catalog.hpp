#pragma once

#include "digitor/remote_plugin_marketplace.hpp"

#include <string>
#include <utility>
#include <vector>

namespace digitor {

// Built-in filters/effects are ordinary signed .digitorfx packages bundled by
// the consumer application. DigitorEngine does not contain effect IDs,
// algorithm enums, commercial policy, or backend-specific ID switches.
// Adding a built-in package therefore requires only adding its catalog entry
// and package assets to the app bundle.
struct BundledPluginCatalogResult final {
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::vector<RemotePluginInstallRecord> installed;
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

class BundledPluginCatalogInstaller final {
 public:
  explicit BundledPluginCatalogInstaller(RemotePluginMarketplace& marketplace)
      : marketplace_(marketplace) {}

  [[nodiscard]] BundledPluginCatalogResult load_and_install(
      RemotePluginCatalog catalog) {
    BundledPluginCatalogResult out{};
    std::string diagnostic;

    // A bundled catalog uses the exact same signature/hash/package/runtime
    // validation as a website/GitHub catalog. Only the transport URL differs.
    for (const auto& entry : catalog.plugins) {
      for (const auto& artifact : entry.artifacts) {
        if (artifact.url.rfind("bundle://", 0) != 0) {
          out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
          out.diagnostic =
              "bundled plugin artifacts must use the bundle:// transport";
          return out;
        }
      }
    }

    const auto loaded = marketplace_.load_catalog(std::move(catalog), &diagnostic);
    if (loaded != DIGITOR_RESULT_OK) {
      out.result = loaded;
      out.diagnostic = std::move(diagnostic);
      return out;
    }

    const auto* loaded_catalog = marketplace_.catalog();
    if (!loaded_catalog) {
      out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      out.diagnostic = "bundled plugin catalog was not retained";
      return out;
    }

    out.installed.reserve(loaded_catalog->plugins.size());
    for (const auto& entry : loaded_catalog->plugins) {
      if (entry.revoked) continue;
      auto installed = marketplace_.install(entry.id);
      if (!installed) {
        out.result = installed.result;
        out.diagnostic = entry.id + ": " + installed.diagnostic;
        return out;
      }
      out.installed.push_back(*installed.record);
    }

    out.result = DIGITOR_RESULT_OK;
    out.diagnostic.clear();
    return out;
  }

 private:
  RemotePluginMarketplace& marketplace_;
};

}  // namespace digitor
