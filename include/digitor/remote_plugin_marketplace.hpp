#pragma once

#include "digitor/digitor.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class RemotePluginKind : std::uint32_t { filter, effect, transition };
enum class RemotePluginBackend : std::uint32_t {
  windows_d3d12,
  windows_vulkan,
  android_vulkan,
  android_gles,
  apple_metal
};

enum class RemotePluginInstallState : std::uint32_t {
  not_installed,
  installed,
  update_available,
  quarantined,
  revoked
};

struct RemotePluginParameter final {
  std::string id;
  std::string label;
  double minimum{};
  double maximum{1.0};
  double default_value{};
  bool keyframeable{true};
};

struct RemotePluginArtifact final {
  RemotePluginBackend backend{RemotePluginBackend::windows_d3d12};
  std::string url;
  std::string sha256;
  std::string package_path;
};

// DigitorEngine intentionally contains no free/paid, subscription, product,
// purchase, trial, preview-right or export-right fields. Consumer apps own all
// commercial policy and submit only operations they have already authorized.
struct RemotePluginCatalogEntry final {
  std::string id;
  std::string display_name;
  std::string version;
  std::string minimum_engine_version;
  RemotePluginKind kind{RemotePluginKind::effect};
  std::string publisher_key_id;
  std::string signature;
  std::vector<RemotePluginParameter> parameters;
  std::vector<RemotePluginArtifact> artifacts;
  bool revoked{};
};

struct RemotePluginCatalog final {
  std::uint32_t schema_version{2};
  std::string catalog_id;
  std::string generated_at;
  std::vector<RemotePluginCatalogEntry> plugins;
};

struct RemotePluginInstallRecord final {
  std::string id;
  std::string version;
  std::string package_path;
  std::string sha256;
  RemotePluginKind kind{RemotePluginKind::effect};
  RemotePluginInstallState state{RemotePluginInstallState::not_installed};
};

using RemotePluginDownload = std::function<bool(
    std::string_view url, std::vector<std::byte>& bytes,
    std::string& diagnostic)>;
using RemotePluginSha256 = std::function<std::string(
    const std::vector<std::byte>& bytes)>;
using RemotePluginSignatureVerifier = std::function<bool(
    std::string_view publisher_key_id, std::string_view canonical_payload,
    std::string_view signature, std::string& diagnostic)>;
using RemotePluginPackageInstaller = std::function<bool(
    const RemotePluginCatalogEntry&, const RemotePluginArtifact&,
    const std::vector<std::byte>& bytes, std::string& installed_path,
    std::string& diagnostic)>;
using RemotePluginRuntimeRegistrar = std::function<bool(
    const RemotePluginCatalogEntry&, std::string_view installed_path,
    std::string& diagnostic)>;
using RemotePluginRuntimeUnregister = std::function<void(std::string_view id)>;

struct RemotePluginMarketplaceBindings final {
  std::string engine_version;
  RemotePluginBackend backend{RemotePluginBackend::windows_d3d12};
  RemotePluginDownload download;
  RemotePluginSha256 sha256;
  RemotePluginSignatureVerifier verify_signature;
  RemotePluginPackageInstaller install_package;
  RemotePluginRuntimeRegistrar register_runtime;
  RemotePluginRuntimeUnregister unregister_runtime;
};

struct RemotePluginOperationResult final {
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  std::optional<RemotePluginInstallRecord> record;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

class RemotePluginMarketplace final {
 public:
  explicit RemotePluginMarketplace(RemotePluginMarketplaceBindings bindings);

  DigitorResult load_catalog(RemotePluginCatalog catalog,
                             std::string* diagnostic = nullptr);
  RemotePluginOperationResult install(std::string_view plugin_id);
  RemotePluginOperationResult update(std::string_view plugin_id);
  DigitorResult uninstall(std::string_view plugin_id,
                          std::string* diagnostic = nullptr);

  const RemotePluginCatalog* catalog() const noexcept;
  std::optional<RemotePluginCatalogEntry> find(std::string_view id) const;
  std::optional<RemotePluginInstallRecord> installed(std::string_view id) const;
  std::vector<RemotePluginCatalogEntry> available(RemotePluginKind kind) const;

 private:
  RemotePluginOperationResult install_impl(
      const RemotePluginCatalogEntry&, bool is_update);

  RemotePluginMarketplaceBindings bindings_;
  std::optional<RemotePluginCatalog> catalog_;
  std::unordered_map<std::string, RemotePluginInstallRecord> installed_;
};

std::string canonical_remote_plugin_payload(
    const RemotePluginCatalogEntry& entry);
bool validate_remote_plugin_catalog_entry(
    const RemotePluginCatalogEntry& entry, std::string& diagnostic) noexcept;

}  // namespace digitor
