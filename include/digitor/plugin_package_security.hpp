#pragma once

#include "digitor/digitor.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

struct PluginPackageEntry final {
  std::string relative_path;
  std::uint64_t compressed_size{};
  std::uint64_t uncompressed_size{};
  bool directory{};
  bool symbolic_link{};
  bool executable{};
};

struct PluginPackageSecurityPolicy final {
  std::uint64_t maximum_archive_bytes{256ull * 1024ull * 1024ull};
  std::uint64_t maximum_uncompressed_bytes{512ull * 1024ull * 1024ull};
  std::uint64_t maximum_single_entry_bytes{128ull * 1024ull * 1024ull};
  std::uint32_t maximum_entries{4096};
  double maximum_expansion_ratio{64.0};
  bool allow_executable_files{false};
  bool allow_symbolic_links{false};
};

struct PluginPackageIdentity final {
  std::string plugin_id;
  std::string version;
  std::string publisher_key_id;
  std::string archive_sha256;
  std::string signature;
  bool revoked{};
};

using PluginPackageSignatureVerifier = std::function<bool(
    const PluginPackageIdentity&, std::string_view canonical_manifest,
    std::string& diagnostic)>;
using PluginPackageStage = std::function<bool(
    const PluginPackageIdentity&, std::string& staged_path,
    std::string& diagnostic)>;
using PluginPackageExtract = std::function<bool(
    std::string_view staged_path, const std::vector<PluginPackageEntry>&,
    std::string& diagnostic)>;
using PluginPackageActivate = std::function<bool(
    std::string_view staged_path, std::string& active_path,
    std::string& diagnostic)>;
using PluginPackageRollback = std::function<void(
    std::string_view staged_path, std::string_view active_path)>;

struct PluginPackageSecurityBindings final {
  PluginPackageSignatureVerifier verify_signature;
  PluginPackageStage create_stage;
  PluginPackageExtract extract_to_stage;
  PluginPackageActivate activate_atomically;
  PluginPackageRollback rollback;
};

struct PluginPackageInstallRequest final {
  PluginPackageIdentity identity;
  std::string canonical_manifest;
  std::uint64_t archive_bytes{};
  std::vector<PluginPackageEntry> entries;
};

struct PluginPackageInstallResult final {
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string active_path;
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] bool validate_plugin_package(
    const PluginPackageInstallRequest&, const PluginPackageSecurityPolicy&,
    std::string& diagnostic) noexcept;

[[nodiscard]] PluginPackageInstallResult install_plugin_package_atomically(
    const PluginPackageInstallRequest&, const PluginPackageSecurityPolicy&,
    const PluginPackageSecurityBindings&) noexcept;

}  // namespace digitor
