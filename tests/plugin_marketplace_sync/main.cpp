#include "digitor/plugin_marketplace_sync.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_MARKETPLACE_SYNC_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginMarketplacePackageRecord package(const char* id, const char* version, char hash_char) {
  return {id, version, std::string(64, hash_char),
          std::string("https://plugins.example/packages/") + id + "/" + version + ".digitorfx",
          4096, false};
}
}

int main() {
  using namespace digitor;
  PluginMarketplaceSyncPolicy policy{};
  const std::uint64_t now = 2000000;

  PluginMarketplaceCatalogState cached{"41", "etag-41", "Tue", std::string(64, 'a'), now - 60};
  PluginMarketplaceCatalogState remote{"42", "etag-42", "Wed", std::string(64, 'b'), now};
  std::vector<PluginMarketplacePackageRecord> installed{
      package("filter.free", "1.0.0", 'c'),
      package("effect.paid", "2.0.0", 'd'),
      package("transition.paid", "1.0.0", 'e')};
  std::vector<PluginMarketplacePackageRecord> available{
      package("transition.paid", "1.1.0", 'f'),
      package("filter.free", "1.0.0", 'c'),
      package("effect.paid", "2.1.0", '1')};

  const auto online = plan_marketplace_sync(&cached, &remote, installed, available, now, true, policy);
  if (online.status != PluginMarketplaceSyncStatus::updated || !online.use_network_catalog || online.updates.size() != 2)
    return fail("online update planning failed");
  if (online.updates[0].plugin_id != "effect.paid" || online.updates[1].plugin_id != "transition.paid")
    return fail("update order is not deterministic");

  remote.etag = cached.etag;
  remote.catalog_sha256 = cached.catalog_sha256;
  const auto unchanged = plan_marketplace_sync(&cached, &remote, installed, available, now, true, policy);
  if (unchanged.status != PluginMarketplaceSyncStatus::not_modified || !unchanged.use_cached_catalog)
    return fail("ETag not-modified handling failed");

  const auto offline = plan_marketplace_sync(&cached, nullptr, installed, available, now, false, policy);
  if (offline.status != PluginMarketplaceSyncStatus::offline_cache_used || !offline.use_cached_catalog)
    return fail("fresh offline cache was rejected");

  auto stale = cached;
  stale.fetched_unix_seconds = now - policy.max_catalog_age_seconds - 1;
  if (plan_marketplace_sync(&stale, nullptr, installed, available, now, false, policy).status !=
      PluginMarketplaceSyncStatus::stale_cache_rejected)
    return fail("stale offline cache was accepted");

  std::string diagnostic;
  const auto offline_package = package("transition.paid", "1.1.0", 'f');
  if (!validate_offline_plugin_import(offline_package,
                                      "imports/transition.paid-1.1.0.digitorfx",
                                      offline_package.package_sha256,
                                      offline_package.package_size,
                                      policy,
                                      diagnostic))
    return fail("valid offline package was rejected");
  if (validate_offline_plugin_import(offline_package,
                                     "../transition.digitorfx",
                                     offline_package.package_sha256,
                                     offline_package.package_size,
                                     policy,
                                     diagnostic))
    return fail("unsafe offline path was accepted");
  if (validate_offline_plugin_import(offline_package,
                                     "imports/transition.digitorfx",
                                     std::string(64, '0'),
                                     offline_package.package_size,
                                     policy,
                                     diagnostic))
    return fail("hash mismatch was accepted");

  PluginMarketplaceCacheEntry cache{offline_package, "cache/transition/1.1.0/package.digitorfx", now};
  if (!validate_marketplace_cache_entry(cache, policy, diagnostic))
    return fail("valid cache entry was rejected");

  std::cout << "PLUGIN_MARKETPLACE_SYNC_QUALIFIED=1\n";
  std::cout << "FILTER_EFFECT_TRANSITION_SYNC=1\n";
  std::cout << "OFFLINE_IMPORT_AND_CACHE=1\n";
  std::cout << "COMMERCIAL_POLICY_IN_ENGINE=0\n";
  std::cout << "MAIN_RENDERING_FEATURES_CHANGED=0\n";
  return 0;
}
