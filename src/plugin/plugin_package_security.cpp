#include "digitor/plugin_package_security.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace digitor {
namespace {

bool valid_token(std::string_view value) noexcept {
  if (value.empty() || value.size() > 160) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '_' || c == '-';
  });
}

bool valid_sha256(std::string_view value) noexcept {
  return value.size() == 64 && std::all_of(value.begin(), value.end(),
      [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool safe_relative_path(std::string_view path) noexcept {
  if (path.empty() || path.size() > 1024 || path.front() == '/' ||
      path.front() == '\\' || path.find('\0') != std::string_view::npos ||
      path.find("\\") != std::string_view::npos ||
      path.find(":") != std::string_view::npos) return false;
  std::size_t start = 0;
  while (start <= path.size()) {
    const auto end = path.find('/', start);
    const auto part = path.substr(start, end == std::string_view::npos
        ? path.size() - start : end - start);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return true;
}

}  // namespace

bool validate_plugin_package(const PluginPackageInstallRequest& request,
                             const PluginPackageSecurityPolicy& policy,
                             std::string& diagnostic) noexcept {
  const auto& id = request.identity;
  if (!valid_token(id.plugin_id) || !valid_token(id.version) ||
      !valid_token(id.publisher_key_id) || !valid_sha256(id.archive_sha256) ||
      id.signature.empty() || id.signature.size() > 4096 ||
      request.canonical_manifest.empty()) {
    diagnostic = "plugin package identity is invalid";
    return false;
  }
  if (id.revoked) {
    diagnostic = "plugin package or publisher is revoked";
    return false;
  }
  if (request.archive_bytes == 0 ||
      request.archive_bytes > policy.maximum_archive_bytes ||
      request.entries.empty() || request.entries.size() > policy.maximum_entries) {
    diagnostic = "plugin package archive exceeds limits";
    return false;
  }

  std::uint64_t total_uncompressed = 0;
  std::vector<std::string> paths;
  for (const auto& entry : request.entries) {
    if (!safe_relative_path(entry.relative_path) ||
        entry.uncompressed_size > policy.maximum_single_entry_bytes ||
        (!policy.allow_symbolic_links && entry.symbolic_link) ||
        (!policy.allow_executable_files && entry.executable) ||
        std::find(paths.begin(), paths.end(), entry.relative_path) != paths.end()) {
      diagnostic = "plugin package entry is unsafe";
      return false;
    }
    paths.push_back(entry.relative_path);
    if (entry.uncompressed_size > policy.maximum_uncompressed_bytes -
        std::min(total_uncompressed, policy.maximum_uncompressed_bytes)) {
      diagnostic = "plugin package expanded size overflows limits";
      return false;
    }
    total_uncompressed += entry.uncompressed_size;
  }
  if (total_uncompressed > policy.maximum_uncompressed_bytes) {
    diagnostic = "plugin package expanded size exceeds limit";
    return false;
  }
  const double ratio = static_cast<double>(total_uncompressed) /
                       static_cast<double>(request.archive_bytes);
  if (!std::isfinite(ratio) || ratio > policy.maximum_expansion_ratio) {
    diagnostic = "plugin package expansion ratio exceeds limit";
    return false;
  }
  diagnostic.clear();
  return true;
}

PluginPackageInstallResult install_plugin_package_atomically(
    const PluginPackageInstallRequest& request,
    const PluginPackageSecurityPolicy& policy,
    const PluginPackageSecurityBindings& bindings) noexcept {
  PluginPackageInstallResult out{};
  std::string staged_path;
  std::string active_path;
  try {
    if (!validate_plugin_package(request, policy, out.diagnostic))
      return out;
    if (!bindings.verify_signature || !bindings.create_stage ||
        !bindings.extract_to_stage || !bindings.activate_atomically) {
      out.diagnostic = "plugin package security bindings are incomplete";
      return out;
    }
    if (!bindings.verify_signature(request.identity,
                                   request.canonical_manifest,
                                   out.diagnostic)) {
      out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      if (out.diagnostic.empty()) out.diagnostic = "plugin signature verification failed";
      return out;
    }
    if (!bindings.create_stage(request.identity, staged_path, out.diagnostic) ||
        staged_path.empty()) {
      out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      if (out.diagnostic.empty()) out.diagnostic = "plugin staging failed";
      return out;
    }
    if (!bindings.extract_to_stage(staged_path, request.entries, out.diagnostic) ||
        !bindings.activate_atomically(staged_path, active_path, out.diagnostic) ||
        active_path.empty()) {
      if (bindings.rollback) bindings.rollback(staged_path, active_path);
      out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      if (out.diagnostic.empty()) out.diagnostic = "plugin activation failed and was rolled back";
      return out;
    }
    out.result = DIGITOR_RESULT_OK;
    out.active_path = std::move(active_path);
    out.diagnostic.clear();
    return out;
  } catch (...) {
    if (bindings.rollback) {
      try { bindings.rollback(staged_path, active_path); } catch (...) {}
    }
    out.result = DIGITOR_RESULT_INTERNAL_ERROR;
    out.diagnostic = "plugin installation callback threw across engine boundary";
    return out;
  }
}

}  // namespace digitor
