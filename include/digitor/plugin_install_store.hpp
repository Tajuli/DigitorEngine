#pragma once

#include "digitor/remote_plugin_marketplace.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace digitor {

struct PluginProjectPin final {
  std::string plugin_id;
  std::string plugin_version;
  std::string package_sha256;
};

enum class PluginPinStatus : std::uint32_t {
  exact_match,
  missing_installation,
  version_mismatch,
  hash_mismatch,
  unavailable
};

struct PluginPinResolution final {
  PluginPinStatus status{PluginPinStatus::unavailable};
  std::optional<RemotePluginInstallRecord> installed;
  std::string diagnostic;
  explicit operator bool() const noexcept {
    return status == PluginPinStatus::exact_match;
  }
};

using PluginInstallLoad = std::function<bool(
    std::vector<RemotePluginInstallRecord>& records, std::string& diagnostic)>;
using PluginInstallSave = std::function<bool(
    const RemotePluginInstallRecord& record, std::string& diagnostic)>;
using PluginInstallRemove = std::function<bool(
    std::string_view plugin_id, std::string& diagnostic)>;

struct PluginInstallStoreBindings final {
  PluginInstallLoad load;
  PluginInstallSave save;
  PluginInstallRemove remove;
};

class PluginInstallStore final {
 public:
  explicit PluginInstallStore(PluginInstallStoreBindings bindings);

  DigitorResult restore(std::string* diagnostic = nullptr) noexcept;
  DigitorResult persist(const RemotePluginInstallRecord& record,
                        std::string* diagnostic = nullptr) noexcept;
  DigitorResult erase(std::string_view plugin_id,
                      std::string* diagnostic = nullptr) noexcept;

  std::optional<RemotePluginInstallRecord> installed(
      std::string_view plugin_id) const;
  std::vector<RemotePluginInstallRecord> records() const;
  PluginPinResolution resolve(const PluginProjectPin& pin) const noexcept;

 private:
  PluginInstallStoreBindings bindings_;
  std::unordered_map<std::string, RemotePluginInstallRecord> records_;
};

[[nodiscard]] bool validate_plugin_install_record(
    const RemotePluginInstallRecord& record,
    std::string& diagnostic) noexcept;
[[nodiscard]] bool validate_plugin_project_pin(
    const PluginProjectPin& pin,
    std::string& diagnostic) noexcept;

}  // namespace digitor
