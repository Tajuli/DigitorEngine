#include "digitor/plugin_distribution_release.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <tuple>

namespace digitor {
namespace {

bool lowercase_sha256(std::string_view value) noexcept {
  return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isdigit(c) || (c >= 'a' && c <= 'f');
  });
}

bool safe_relative_path(std::string_view value) noexcept {
  if (value.empty() || value.front() == '/' || value.front() == '\\') return false;
  if (value.size() > 240 || value.find("..") != std::string_view::npos ||
      value.find(':') != std::string_view::npos || value.find('\\') != std::string_view::npos)
    return false;
  return true;
}

bool https_url(std::string_view value) noexcept {
  return value.size() <= 2048 && value.starts_with("https://");
}

bool token(std::string_view value, std::size_t limit) noexcept {
  if (value.empty() || value.size() > limit) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '_' || c == '-';
  });
}

std::string escaped(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char c : value) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

const char* kind_name(PluginDistributionKind kind) noexcept {
  switch (kind) {
    case PluginDistributionKind::filter: return "filter";
    case PluginDistributionKind::effect: return "effect";
    case PluginDistributionKind::transition: return "transition";
  }
  return "unknown";
}

}  // namespace

bool validate_plugin_distribution_release(
    const PluginDistributionRelease& release,
    std::string& diagnostic) noexcept {
  if (!token(release.plugin_id, 160) || !token(release.version, 48) ||
      !token(release.publisher_id, 160) || !token(release.minimum_engine_version, 48)) {
    diagnostic = "plugin distribution identity is invalid";
    return false;
  }
  if (!safe_relative_path(release.package_file_name) ||
      !release.package_file_name.ends_with(".digitorfx")) {
    diagnostic = "plugin package filename is unsafe or has the wrong extension";
    return false;
  }
  if (!lowercase_sha256(release.package_sha256) || release.package_signature.empty()) {
    diagnostic = "plugin package identity or signature is invalid";
    return false;
  }
  if (!https_url(release.download_url) ||
      (!release.thumbnail_url.empty() && !https_url(release.thumbnail_url)) ||
      (!release.preview_media_url.empty() && !https_url(release.preview_media_url))) {
    diagnostic = "plugin distribution URL must use HTTPS";
    return false;
  }
  if (release.supported_backends.empty() || release.supported_backends.size() > 8 ||
      release.artifacts.empty() || release.artifacts.size() > 64) {
    diagnostic = "plugin backend or artifact set is invalid";
    return false;
  }
  std::set<std::string> backends;
  for (const auto& backend : release.supported_backends) {
    if (!token(backend, 48) || !backends.insert(backend).second) {
      diagnostic = "plugin backend is invalid or duplicated";
      return false;
    }
  }
  std::set<std::string> paths;
  std::uint64_t expanded = 0;
  for (const auto& artifact : release.artifacts) {
    if (!safe_relative_path(artifact.relative_path) ||
        !lowercase_sha256(artifact.sha256) || artifact.size_bytes == 0 ||
        !paths.insert(artifact.relative_path).second) {
      diagnostic = "plugin artifact is invalid or duplicated";
      return false;
    }
    if (expanded > 512ull * 1024ull * 1024ull - artifact.size_bytes) {
      diagnostic = "plugin expanded artifact size exceeds limit";
      return false;
    }
    expanded += artifact.size_bytes;
  }
  diagnostic.clear();
  return true;
}

bool build_plugin_distribution_bundle(
    const PluginDistributionCatalog& catalog,
    PluginDistributionBundle& bundle,
    std::string& diagnostic) noexcept {
  if (catalog.schema_version != 1 || catalog.generated_at_utc.empty() ||
      !token(catalog.publisher_key_id, 160) || catalog.catalog_signature.empty() ||
      catalog.releases.empty() || catalog.releases.size() > 10000) {
    diagnostic = "plugin distribution catalog header is invalid";
    return false;
  }
  std::vector<PluginDistributionRelease> releases = catalog.releases;
  std::sort(releases.begin(), releases.end(), [](const auto& a, const auto& b) {
    return std::tie(a.plugin_id, a.version) < std::tie(b.plugin_id, b.version);
  });
  std::set<std::pair<std::string, std::string>> identities;
  for (const auto& release : releases) {
    if (!validate_plugin_distribution_release(release, diagnostic)) return false;
    if (!identities.emplace(release.plugin_id, release.version).second) {
      diagnostic = "plugin release identity is duplicated";
      return false;
    }
  }

  std::ostringstream json;
  json << "{\"schema_version\":1,\"generated_at_utc\":\""
       << escaped(catalog.generated_at_utc) << "\",\"publisher_key_id\":\""
       << escaped(catalog.publisher_key_id) << "\",\"releases\":[";
  bool first = true;
  bundle.publish_paths.clear();
  for (const auto& release : releases) {
    if (!first) json << ',';
    first = false;
    json << "{\"plugin_id\":\"" << escaped(release.plugin_id)
         << "\",\"version\":\"" << escaped(release.version)
         << "\",\"kind\":\"" << kind_name(release.kind)
         << "\",\"package_sha256\":\"" << release.package_sha256
         << "\",\"download_url\":\"" << escaped(release.download_url)
         << "\",\"revoked\":" << (release.revoked ? "true" : "false") << '}';
    bundle.publish_paths.push_back("packages/" + release.plugin_id + "/" +
                                   release.version + "/" + release.package_file_name);
  }
  json << "]}";
  bundle.catalog_json = json.str();
  bundle.catalog_signing_payload = bundle.catalog_json + "\nkey=" + catalog.publisher_key_id;
  bundle.publish_paths.push_back("catalog/v1/catalog.json");
  bundle.publish_paths.push_back("catalog/v1/catalog.sig");
  diagnostic.clear();
  return true;
}

bool verify_plugin_distribution_import_fixture(
    const PluginDistributionCatalog& catalog,
    std::string_view plugin_id,
    std::string_view exact_version,
    std::string_view package_sha256,
    std::string& diagnostic) noexcept {
  if (!lowercase_sha256(package_sha256)) {
    diagnostic = "requested package SHA-256 is invalid";
    return false;
  }
  const auto match = std::find_if(catalog.releases.begin(), catalog.releases.end(),
                                  [&](const auto& release) {
    return release.plugin_id == plugin_id && release.version == exact_version;
  });
  if (match == catalog.releases.end()) {
    diagnostic = "exact plugin release is not present in the catalog";
    return false;
  }
  if (match->revoked) {
    diagnostic = "exact plugin release is revoked";
    return false;
  }
  if (match->package_sha256 != package_sha256) {
    diagnostic = "plugin package SHA-256 does not match the catalog";
    return false;
  }
  diagnostic.clear();
  return true;
}

}  // namespace digitor
