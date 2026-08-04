#include "digitor/plugin_marketplace_sync.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <unordered_map>

namespace digitor {
namespace {
bool valid_sha256(std::string_view value) {
  return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isdigit(c) || (c >= 'a' && c <= 'f');
  });
}

bool safe_relative_path(std::string_view path) {
  if (path.empty() || path.front() == '/' || path.front() == '\\') return false;
  if (path.size() > 1 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') return false;
  std::size_t begin = 0;
  while (begin <= path.size()) {
    const auto end = path.find_first_of("/\\", begin);
    const auto part = path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == std::string_view::npos) break;
    begin = end + 1;
  }
  return true;
}

bool newer_version(std::string_view candidate, std::string_view installed) {
  return candidate != installed && candidate > installed;
}

std::string key_of(const PluginMarketplacePackageRecord& record) {
  return record.plugin_id + "@" + record.version;
}
}  // namespace

bool validate_marketplace_package_record(const PluginMarketplacePackageRecord& record,
                                           const PluginMarketplaceSyncPolicy& policy,
                                           std::string& diagnostic) noexcept {
  try {
    diagnostic.clear();
    if (record.plugin_id.empty() || record.version.empty()) {
      diagnostic = "plugin identity is incomplete";
      return false;
    }
    if (!valid_sha256(record.package_sha256)) {
      diagnostic = "package SHA-256 is invalid";
      return false;
    }
    if (record.package_size == 0 || record.package_size > policy.max_package_size) {
      diagnostic = "package size is outside policy";
      return false;
    }
    if (policy.require_https && record.download_url.rfind("https://", 0) != 0) {
      diagnostic = "package download URL must use HTTPS";
      return false;
    }
    return true;
  } catch (...) {
    diagnostic = "package validation failed";
    return false;
  }
}

bool validate_marketplace_cache_entry(const PluginMarketplaceCacheEntry& entry,
                                       const PluginMarketplaceSyncPolicy& policy,
                                       std::string& diagnostic) noexcept {
  try {
    if (!validate_marketplace_package_record(entry.package, policy, diagnostic)) return false;
    if (!safe_relative_path(entry.local_path)) {
      diagnostic = "cache path is unsafe";
      return false;
    }
    if (entry.verified_unix_seconds == 0) {
      diagnostic = "cache entry has not been integrity verified";
      return false;
    }
    return true;
  } catch (...) {
    diagnostic = "cache validation failed";
    return false;
  }
}

PluginMarketplaceSyncPlan plan_marketplace_sync(
    const PluginMarketplaceCatalogState* cached,
    const PluginMarketplaceCatalogState* remote,
    const std::vector<PluginMarketplacePackageRecord>& installed,
    const std::vector<PluginMarketplacePackageRecord>& available,
    std::uint64_t now_unix_seconds,
    bool network_available,
    const PluginMarketplaceSyncPolicy& policy) noexcept {
  PluginMarketplaceSyncPlan plan{};
  try {
    std::string diagnostic;
    std::set<std::string> identities;
    for (const auto& record : available) {
      if (!validate_marketplace_package_record(record, policy, diagnostic)) {
        plan.status = PluginMarketplaceSyncStatus::invalid_catalog;
        plan.diagnostic = diagnostic;
        return plan;
      }
      if (!identities.insert(key_of(record)).second) {
        plan.status = PluginMarketplaceSyncStatus::invalid_catalog;
        plan.diagnostic = "catalog contains duplicate plugin identity";
        return plan;
      }
    }

    if (!network_available || remote == nullptr) {
      if (!policy.allow_offline_cache || cached == nullptr || now_unix_seconds < cached->fetched_unix_seconds ||
          now_unix_seconds - cached->fetched_unix_seconds > policy.max_catalog_age_seconds ||
          !valid_sha256(cached->catalog_sha256)) {
        plan.status = PluginMarketplaceSyncStatus::stale_cache_rejected;
        plan.diagnostic = "no trusted fresh catalog is available";
        return plan;
      }
      plan.status = PluginMarketplaceSyncStatus::offline_cache_used;
      plan.use_cached_catalog = true;
    } else {
      if (!valid_sha256(remote->catalog_sha256) || remote->catalog_version.empty()) {
        plan.status = PluginMarketplaceSyncStatus::invalid_catalog;
        plan.diagnostic = "remote catalog identity is invalid";
        return plan;
      }
      if (cached != nullptr && !remote->etag.empty() && remote->etag == cached->etag &&
          remote->catalog_sha256 == cached->catalog_sha256) {
        plan.status = PluginMarketplaceSyncStatus::not_modified;
        plan.use_cached_catalog = true;
      } else {
        plan.status = PluginMarketplaceSyncStatus::updated;
        plan.use_network_catalog = true;
      }
    }

    std::unordered_map<std::string, PluginMarketplacePackageRecord> installed_by_id;
    for (const auto& record : installed) installed_by_id[record.plugin_id] = record;
    for (const auto& record : available) {
      if (record.revoked) continue;
      const auto it = installed_by_id.find(record.plugin_id);
      if (it != installed_by_id.end() && newer_version(record.version, it->second.version)) {
        plan.updates.push_back(record);
      }
    }
    std::sort(plan.updates.begin(), plan.updates.end(), [](const auto& a, const auto& b) {
      return a.plugin_id < b.plugin_id || (a.plugin_id == b.plugin_id && a.version < b.version);
    });
    return plan;
  } catch (...) {
    plan.status = PluginMarketplaceSyncStatus::invalid_catalog;
    plan.diagnostic = "marketplace sync planning failed";
    return plan;
  }
}

bool validate_offline_plugin_import(const PluginMarketplacePackageRecord& expected,
                                    std::string_view local_path,
                                    std::string_view actual_sha256,
                                    std::uint64_t actual_size,
                                    const PluginMarketplaceSyncPolicy& policy,
                                    std::string& diagnostic) noexcept {
  try {
    if (!validate_marketplace_package_record(expected, policy, diagnostic)) return false;
    if (expected.revoked) {
      diagnostic = "revoked plugin package cannot be imported";
      return false;
    }
    if (!safe_relative_path(local_path) || local_path.size() < 10 ||
        local_path.substr(local_path.size() - 10) != ".digitorfx") {
      diagnostic = "offline package path is invalid";
      return false;
    }
    if (!valid_sha256(actual_sha256) || actual_sha256 != expected.package_sha256) {
      diagnostic = "offline package hash does not match catalog identity";
      return false;
    }
    if (actual_size == 0 || actual_size != expected.package_size || actual_size > policy.max_package_size) {
      diagnostic = "offline package size does not match catalog identity";
      return false;
    }
    diagnostic.clear();
    return true;
  } catch (...) {
    diagnostic = "offline import validation failed";
    return false;
  }
}

}  // namespace digitor
