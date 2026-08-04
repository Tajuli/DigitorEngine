#include "digitor/plugin_install_store.hpp"

#include <algorithm>
#include <cctype>
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
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return std::isxdigit(c) != 0;
         });
}

DigitorResult fail(DigitorResult result, std::string message,
                   std::string* diagnostic) noexcept {
  if (diagnostic) *diagnostic = std::move(message);
  return result;
}

}  // namespace

bool validate_plugin_install_record(const RemotePluginInstallRecord& record,
                                    std::string& diagnostic) noexcept {
  if (!valid_token(record.id) || !valid_token(record.version) ||
      !valid_sha256(record.sha256) || record.package_path.empty() ||
      record.package_path.size() > 4096 ||
      record.package_path.find("..") != std::string::npos) {
    diagnostic = "persistent plugin installation record is invalid";
    return false;
  }
  diagnostic.clear();
  return true;
}

bool validate_plugin_project_pin(const PluginProjectPin& pin,
                                 std::string& diagnostic) noexcept {
  if (!valid_token(pin.plugin_id) || !valid_token(pin.plugin_version) ||
      !valid_sha256(pin.package_sha256)) {
    diagnostic = "plugin project pin is invalid";
    return false;
  }
  diagnostic.clear();
  return true;
}

PluginInstallStore::PluginInstallStore(PluginInstallStoreBindings bindings)
    : bindings_(std::move(bindings)) {}

DigitorResult PluginInstallStore::restore(std::string* diagnostic) noexcept {
  try {
    if (!bindings_.load)
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "plugin installation load binding is unavailable", diagnostic);
    std::vector<RemotePluginInstallRecord> loaded;
    std::string local;
    if (!bindings_.load(loaded, local))
      return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                  local.empty() ? "plugin installation restore failed" : local,
                  diagnostic);
    if (loaded.size() > 4096)
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "persistent plugin installation record limit exceeded",
                  diagnostic);
    std::unordered_map<std::string, RemotePluginInstallRecord> next;
    for (const auto& record : loaded) {
      if (!validate_plugin_install_record(record, local) ||
          !next.emplace(record.id, record).second)
        return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                    local.empty() ? "duplicate persistent plugin id" : local,
                    diagnostic);
    }
    records_ = std::move(next);
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  } catch (...) {
    return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                "plugin installation restore raised an exception", diagnostic);
  }
}

DigitorResult PluginInstallStore::persist(
    const RemotePluginInstallRecord& record,
    std::string* diagnostic) noexcept {
  try {
    std::string local;
    if (!validate_plugin_install_record(record, local))
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT, local, diagnostic);
    if (!bindings_.save)
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "plugin installation save binding is unavailable", diagnostic);
    if (!bindings_.save(record, local))
      return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                  local.empty() ? "plugin installation persistence failed" : local,
                  diagnostic);
    records_[record.id] = record;
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  } catch (...) {
    return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                "plugin installation persistence raised an exception", diagnostic);
  }
}

DigitorResult PluginInstallStore::erase(std::string_view plugin_id,
                                        std::string* diagnostic) noexcept {
  try {
    if (!valid_token(plugin_id))
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "plugin id is invalid", diagnostic);
    if (!bindings_.remove)
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "plugin installation remove binding is unavailable", diagnostic);
    std::string local;
    if (!bindings_.remove(plugin_id, local))
      return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                  local.empty() ? "plugin installation removal failed" : local,
                  diagnostic);
    records_.erase(std::string(plugin_id));
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  } catch (...) {
    return fail(DIGITOR_RESULT_INTERNAL_ERROR,
                "plugin installation removal raised an exception", diagnostic);
  }
}

std::optional<RemotePluginInstallRecord> PluginInstallStore::installed(
    std::string_view plugin_id) const {
  const auto it = records_.find(std::string(plugin_id));
  return it == records_.end() ? std::nullopt
                              : std::optional<RemotePluginInstallRecord>(it->second);
}

std::vector<RemotePluginInstallRecord> PluginInstallStore::records() const {
  std::vector<RemotePluginInstallRecord> out;
  out.reserve(records_.size());
  for (const auto& item : records_) out.push_back(item.second);
  return out;
}

PluginPinResolution PluginInstallStore::resolve(
    const PluginProjectPin& pin) const noexcept {
  PluginPinResolution out{};
  std::string local;
  if (!validate_plugin_project_pin(pin, local)) {
    out.status = PluginPinStatus::unavailable;
    out.diagnostic = std::move(local);
    return out;
  }
  const auto record = installed(pin.plugin_id);
  if (!record) {
    out.status = PluginPinStatus::missing_installation;
    out.diagnostic = "exact plugin installation is missing";
    return out;
  }
  out.installed = record;
  if (record->version != pin.plugin_version) {
    out.status = PluginPinStatus::version_mismatch;
    out.diagnostic = "installed plugin version does not match project pin";
    return out;
  }
  if (record->sha256 != pin.package_sha256) {
    out.status = PluginPinStatus::hash_mismatch;
    out.diagnostic = "installed plugin package hash does not match project pin";
    return out;
  }
  if (record->state == RemotePluginInstallState::revoked ||
      record->state == RemotePluginInstallState::quarantined) {
    out.status = PluginPinStatus::unavailable;
    out.diagnostic = "pinned plugin installation is unavailable";
    return out;
  }
  out.status = PluginPinStatus::exact_match;
  out.diagnostic.clear();
  return out;
}

}  // namespace digitor
