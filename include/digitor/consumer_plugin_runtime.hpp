#pragma once

#include "digitor/remote_plugin_marketplace.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class ConsumerPluginSurface : std::uint32_t { preview, export_frame };
enum class ConsumerPluginOperation : std::uint32_t {
  browse,
  import_plugin,
  apply_preview,
  apply_export,
  remove
};

struct ConsumerPluginInstance final {
  std::string instance_id;
  std::string plugin_id;
  std::string plugin_version;
  RemotePluginKind kind{RemotePluginKind::effect};
  RemotePluginTier tier{RemotePluginTier::free};
  std::unordered_map<std::string, double> parameters;
  bool enabled{true};
};

struct ConsumerPluginView final {
  RemotePluginCatalogEntry plugin;
  RemotePluginInstallState install_state{RemotePluginInstallState::not_installed};
  bool visible{true};
  bool import_allowed{};
  bool preview_allowed{};
  bool export_allowed{};
};

// DigitorEngine does not inspect subscriptions, purchases, plans or accounts.
// The consumer app is authoritative and returns true when the requested
// operation is allowed. Returning false blocks only that operation.
using ConsumerPluginAuthorize = std::function<bool(
    const RemotePluginCatalogEntry&, ConsumerPluginOperation,
    std::string_view project_or_clip_id, std::string& diagnostic)>;
using ConsumerPluginApply = std::function<bool(
    const ConsumerPluginInstance&, ConsumerPluginSurface,
    std::string_view project_or_clip_id, std::string& diagnostic)>;
using ConsumerPluginRemove = std::function<void(
    std::string_view instance_id, ConsumerPluginSurface,
    std::string_view project_or_clip_id)>;

struct ConsumerPluginRuntimeBindings final {
  ConsumerPluginAuthorize authorize;
  ConsumerPluginApply apply_instance;
  ConsumerPluginRemove remove_instance;
};

struct ConsumerPluginApplyResult final {
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  std::optional<ConsumerPluginInstance> instance;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

class ConsumerPluginRuntime final {
 public:
  ConsumerPluginRuntime(RemotePluginMarketplace& marketplace,
                        ConsumerPluginRuntimeBindings bindings);

  std::vector<ConsumerPluginView> browse(RemotePluginKind kind) const;
  RemotePluginOperationResult import_plugin(std::string_view plugin_id);
  ConsumerPluginApplyResult apply(
      std::string_view plugin_id, std::string_view instance_id,
      std::string_view project_or_clip_id,
      const std::unordered_map<std::string, double>& parameters,
      ConsumerPluginSurface surface);
  DigitorResult remove(std::string_view instance_id,
                       std::string_view project_or_clip_id,
                       ConsumerPluginSurface surface,
                       std::string* diagnostic = nullptr);

  std::optional<ConsumerPluginInstance> instance(
      std::string_view instance_id) const;
  std::vector<ConsumerPluginInstance> instances() const;

 private:
  bool authorized(const RemotePluginCatalogEntry& entry,
                  ConsumerPluginOperation operation,
                  std::string_view project_or_clip_id,
                  std::string& diagnostic) const;
  bool validate_parameters(
      const RemotePluginCatalogEntry& entry,
      const std::unordered_map<std::string, double>& parameters,
      std::string& diagnostic) const;

  RemotePluginMarketplace& marketplace_;
  ConsumerPluginRuntimeBindings bindings_;
  std::unordered_map<std::string, ConsumerPluginInstance> instances_;
};

}  // namespace digitor
