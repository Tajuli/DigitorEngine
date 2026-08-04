#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

enum class PluginMarketplaceSyncStatus : std::uint32_t {
  updated,
  not_modified,
  offline_cache_used,
  stale_cache_rejected,
  invalid_catalog,
  integrity_failure
};

struct PluginMarketplaceCatalogState final {
  std::string catalog_version;
  std::string etag;
  std::string last_modified;
  std::string catalog_sha256;
  std::uint64_t fetched_unix_seconds{0};
};

struct PluginMarketplacePackageRecord final {
  std::string plugin_id;
  std::string version;
  std::string package_sha256;
  std::string download_url;
  std::uint64_t package_size{0};
  bool revoked{false};
};

struct PluginMarketplaceCacheEntry final {
  PluginMarketplacePackageRecord package;
  std::string local_path;
  std::uint64_t verified_unix_seconds{0};
};

struct PluginMarketplaceSyncPolicy final {
  std::uint64_t max_catalog_age_seconds{7u * 24u * 60u * 60u};
  std::uint64_t max_package_size{512u * 1024u * 1024u};
  bool allow_offline_cache{true};
  bool require_https{true};
};

struct PluginMarketplaceSyncPlan final {
  PluginMarketplaceSyncStatus status{PluginMarketplaceSyncStatus::invalid_catalog};
  bool use_network_catalog{false};
  bool use_cached_catalog{false};
  std::vector<PluginMarketplacePackageRecord> updates;
  std::string diagnostic;
};

[[nodiscard]] bool validate_marketplace_package_record(
    const PluginMarketplacePackageRecord& record,
    const PluginMarketplaceSyncPolicy& policy,
    std::string& diagnostic) noexcept;

[[nodiscard]] bool validate_marketplace_cache_entry(
    const PluginMarketplaceCacheEntry& entry,
    const PluginMarketplaceSyncPolicy& policy,
    std::string& diagnostic) noexcept;

[[nodiscard]] PluginMarketplaceSyncPlan plan_marketplace_sync(
    const PluginMarketplaceCatalogState* cached,
    const PluginMarketplaceCatalogState* remote,
    const std::vector<PluginMarketplacePackageRecord>& installed,
    const std::vector<PluginMarketplacePackageRecord>& available,
    std::uint64_t now_unix_seconds,
    bool network_available,
    const PluginMarketplaceSyncPolicy& policy) noexcept;

[[nodiscard]] bool validate_offline_plugin_import(
    const PluginMarketplacePackageRecord& expected,
    std::string_view local_path,
    std::string_view actual_sha256,
    std::uint64_t actual_size,
    const PluginMarketplaceSyncPolicy& policy,
    std::string& diagnostic) noexcept;

}  // namespace digitor
