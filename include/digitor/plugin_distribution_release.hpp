#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

enum class PluginDistributionKind : std::uint32_t {
  filter,
  effect,
  transition
};

struct PluginDistributionArtifact final {
  std::string relative_path;
  std::string sha256;
  std::uint64_t size_bytes{};
};

struct PluginDistributionRelease final {
  std::string plugin_id;
  std::string version;
  PluginDistributionKind kind{PluginDistributionKind::filter};
  std::string publisher_id;
  std::string package_file_name;
  std::string package_sha256;
  std::string package_signature;
  std::string minimum_engine_version;
  std::string download_url;
  std::string thumbnail_url;
  std::string preview_media_url;
  std::vector<std::string> supported_backends;
  std::vector<PluginDistributionArtifact> artifacts;
  bool revoked{false};
};

struct PluginDistributionCatalog final {
  std::uint32_t schema_version{1};
  std::string generated_at_utc;
  std::string publisher_key_id;
  std::string catalog_signature;
  std::vector<PluginDistributionRelease> releases;
};

struct PluginDistributionBundle final {
  std::string catalog_json;
  std::string catalog_signing_payload;
  std::vector<std::string> publish_paths;
};

[[nodiscard]] bool validate_plugin_distribution_release(
    const PluginDistributionRelease& release,
    std::string& diagnostic) noexcept;

[[nodiscard]] bool build_plugin_distribution_bundle(
    const PluginDistributionCatalog& catalog,
    PluginDistributionBundle& bundle,
    std::string& diagnostic) noexcept;

[[nodiscard]] bool verify_plugin_distribution_import_fixture(
    const PluginDistributionCatalog& catalog,
    std::string_view plugin_id,
    std::string_view exact_version,
    std::string_view package_sha256,
    std::string& diagnostic) noexcept;

}  // namespace digitor
