#include "digitor/remote_plugin_marketplace.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace digitor {
namespace {

bool valid_token(std::string_view value) noexcept {
  if (value.empty() || value.size() > 160) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '_' || c == '-';
  });
}

const RemotePluginArtifact* artifact_for(
    const RemotePluginCatalogEntry& entry,
    RemotePluginBackend backend) noexcept {
  const auto it = std::find_if(entry.artifacts.begin(), entry.artifacts.end(),
      [backend](const RemotePluginArtifact& value) {
        return value.backend == backend;
      });
  return it == entry.artifacts.end() ? nullptr : &*it;
}

bool version_at_least(std::string_view current,
                      std::string_view minimum) noexcept {
  auto read = [](std::string_view v, std::size_t& pos) {
    unsigned value = 0;
    bool any = false;
    while (pos < v.size() && std::isdigit(static_cast<unsigned char>(v[pos]))) {
      any = true;
      value = value * 10u + static_cast<unsigned>(v[pos++] - '0');
    }
    if (pos < v.size() && v[pos] == '.') ++pos;
    return std::pair{value, any};
  };
  std::size_t a = 0, b = 0;
  for (int i = 0; i < 3; ++i) {
    const auto [cv, ca] = read(current, a);
    const auto [mv, ma] = read(minimum, b);
    if (!ca || !ma) return false;
    if (cv != mv) return cv > mv;
  }
  return true;
}

RemotePluginOperationResult failure(DigitorResult result,
                                    std::string diagnostic) {
  RemotePluginOperationResult out{};
  out.result = result;
  out.diagnostic = std::move(diagnostic);
  return out;
}

}  // namespace

std::string canonical_remote_plugin_payload(
    const RemotePluginCatalogEntry& entry) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << "digitor-plugin-v2\n" << entry.id << '\n' << entry.version << '\n'
      << entry.minimum_engine_version << '\n'
      << static_cast<std::uint32_t>(entry.kind) << '\n';
  for (const auto& parameter : entry.parameters) {
    out << parameter.id << '|' << parameter.minimum << '|'
        << parameter.maximum << '|' << parameter.default_value << '|'
        << parameter.keyframeable << '|'
        << canonical_plugin_parameter_ui_schema(parameter.ui) << '\n';
  }
  for (const auto& artifact : entry.artifacts) {
    out << static_cast<std::uint32_t>(artifact.backend) << '|'
        << artifact.url << '|' << artifact.sha256 << '|'
        << artifact.package_path << '\n';
  }
  return out.str();
}

bool validate_remote_plugin_catalog_entry(
    const RemotePluginCatalogEntry& entry,
    std::string& diagnostic) noexcept {
  if (!valid_token(entry.id) || !valid_token(entry.version) ||
      !valid_token(entry.minimum_engine_version) ||
      !valid_token(entry.publisher_key_id) || entry.display_name.empty()) {
    diagnostic = "remote plugin identity metadata is invalid";
    return false;
  }
  if (entry.signature.empty() || entry.signature.size() > 1024) {
    diagnostic = "remote plugin signature is missing or oversized";
    return false;
  }
  if (entry.parameters.size() > 128 || entry.artifacts.empty() ||
      entry.artifacts.size() > 8) {
    diagnostic = "remote plugin exceeds parameter or artifact limits";
    return false;
  }
  for (const auto& parameter : entry.parameters) {
    if (!valid_token(parameter.id) || parameter.label.empty() ||
        parameter.minimum > parameter.maximum ||
        parameter.default_value < parameter.minimum ||
        parameter.default_value > parameter.maximum ||
        !validate_plugin_parameter_ui_schema(parameter.ui, diagnostic)) {
      if (diagnostic.empty()) diagnostic = "remote plugin parameter schema is invalid";
      return false;
    }
  }
  for (const auto& artifact : entry.artifacts) {
    if (artifact.url.empty() || artifact.url.size() > 2048 ||
        artifact.sha256.size() != 64 || artifact.package_path.empty() ||
        artifact.package_path.find("..") != std::string::npos) {
      diagnostic = "remote plugin artifact metadata is invalid";
      return false;
    }
  }
  diagnostic.clear();
  return true;
}

RemotePluginMarketplace::RemotePluginMarketplace(
    RemotePluginMarketplaceBindings bindings)
    : bindings_(std::move(bindings)) {}

DigitorResult RemotePluginMarketplace::load_catalog(
    RemotePluginCatalog catalog, std::string* diagnostic) {
  std::string local;
  if (catalog.schema_version != 2 || catalog.catalog_id.empty() ||
      catalog.plugins.size() > 4096) {
    local = "remote plugin catalog header is invalid";
    if (diagnostic) *diagnostic = local;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  std::unordered_map<std::string, bool> ids;
  for (const auto& entry : catalog.plugins) {
    if (!validate_remote_plugin_catalog_entry(entry, local) ||
        !ids.emplace(entry.id, true).second) {
      if (local.empty()) local = "remote plugin catalog contains duplicate ids";
      if (diagnostic) *diagnostic = local;
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    if (!bindings_.verify_signature ||
        !bindings_.verify_signature(entry.publisher_key_id,
                                    canonical_remote_plugin_payload(entry),
                                    entry.signature, local)) {
      if (local.empty()) local = "remote plugin signature verification failed";
      if (diagnostic) *diagnostic = local;
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
  }
  catalog_ = std::move(catalog);
  for (auto& [id, record] : installed_) {
    const auto item = find(id);
    if (!item) continue;
    if (item->revoked) record.state = RemotePluginInstallState::revoked;
    else if (item->version != record.version)
      record.state = RemotePluginInstallState::update_available;
  }
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

RemotePluginOperationResult RemotePluginMarketplace::install(
    std::string_view plugin_id) {
  const auto entry = find(plugin_id);
  if (!entry) return failure(DIGITOR_RESULT_INVALID_ARGUMENT,
                             "remote plugin is absent from the catalog");
  return install_impl(*entry, false);
}

RemotePluginOperationResult RemotePluginMarketplace::update(
    std::string_view plugin_id) {
  const auto entry = find(plugin_id);
  if (!entry) return failure(DIGITOR_RESULT_INVALID_ARGUMENT,
                             "remote plugin is absent from the catalog");
  return install_impl(*entry, true);
}

RemotePluginOperationResult RemotePluginMarketplace::install_impl(
    const RemotePluginCatalogEntry& entry, bool is_update) {
  if (entry.revoked)
    return failure(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                   "remote plugin has been revoked");
  if (!version_at_least(bindings_.engine_version,
                        entry.minimum_engine_version))
    return failure(DIGITOR_RESULT_UNSUPPORTED,
                   "remote plugin requires a newer engine version");

  const auto* artifact = artifact_for(entry, bindings_.backend);
  if (!artifact)
    return failure(DIGITOR_RESULT_UNSUPPORTED,
                   "remote plugin does not support the selected backend");
  if (!bindings_.download || !bindings_.sha256 ||
      !bindings_.install_package || !bindings_.register_runtime)
    return failure(DIGITOR_RESULT_INVALID_ARGUMENT,
                   "remote plugin marketplace host bindings are incomplete");

  std::vector<std::byte> bytes;
  std::string diagnostic;
  if (!bindings_.download(artifact->url, bytes, diagnostic) ||
      bytes.empty() || bytes.size() > 256u * 1024u * 1024u)
    return failure(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                   diagnostic.empty() ? "remote plugin download failed" : diagnostic);
  if (bindings_.sha256(bytes) != artifact->sha256)
    return failure(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                   "remote plugin package hash mismatch");

  std::string installed_path;
  if (!bindings_.install_package(entry, *artifact, bytes, installed_path,
                                 diagnostic) || installed_path.empty())
    return failure(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                   diagnostic.empty() ? "remote plugin installation failed" : diagnostic);
  if (!bindings_.register_runtime(entry, installed_path, diagnostic))
    return failure(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                   diagnostic.empty() ? "remote plugin runtime registration failed" : diagnostic);

  RemotePluginInstallRecord record{};
  record.id = entry.id;
  record.version = entry.version;
  record.package_path = std::move(installed_path);
  record.sha256 = artifact->sha256;
  record.kind = entry.kind;
  record.state = RemotePluginInstallState::installed;
  installed_[entry.id] = record;

  RemotePluginOperationResult out{};
  out.result = DIGITOR_RESULT_OK;
  out.diagnostic = is_update ? "remote plugin updated" : "remote plugin installed";
  out.record = record;
  return out;
}

DigitorResult RemotePluginMarketplace::uninstall(
    std::string_view plugin_id, std::string* diagnostic) {
  const auto it = installed_.find(std::string(plugin_id));
  if (it == installed_.end()) {
    if (diagnostic) *diagnostic = "remote plugin is not installed";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (bindings_.unregister_runtime) bindings_.unregister_runtime(plugin_id);
  installed_.erase(it);
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

const RemotePluginCatalog* RemotePluginMarketplace::catalog() const noexcept {
  return catalog_ ? &*catalog_ : nullptr;
}

std::optional<RemotePluginCatalogEntry> RemotePluginMarketplace::find(
    std::string_view id) const {
  if (!catalog_) return std::nullopt;
  const auto it = std::find_if(catalog_->plugins.begin(), catalog_->plugins.end(),
      [id](const RemotePluginCatalogEntry& entry) { return entry.id == id; });
  return it == catalog_->plugins.end() ? std::nullopt
                                       : std::optional<RemotePluginCatalogEntry>(*it);
}

std::optional<RemotePluginInstallRecord> RemotePluginMarketplace::installed(
    std::string_view id) const {
  const auto it = installed_.find(std::string(id));
  return it == installed_.end() ? std::nullopt
                                : std::optional<RemotePluginInstallRecord>(it->second);
}

std::vector<RemotePluginCatalogEntry> RemotePluginMarketplace::available(
    RemotePluginKind kind) const {
  std::vector<RemotePluginCatalogEntry> out;
  if (!catalog_) return out;
  for (const auto& entry : catalog_->plugins)
    if (entry.kind == kind && !entry.revoked &&
        artifact_for(entry, bindings_.backend)) out.push_back(entry);
  return out;
}

}  // namespace digitor
