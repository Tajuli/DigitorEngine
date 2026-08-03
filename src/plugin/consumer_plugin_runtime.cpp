#include "digitor/consumer_plugin_runtime.hpp"

#include <algorithm>
#include <utility>

namespace digitor {
namespace {

ConsumerPluginApplyResult apply_failure(DigitorResult result,
                                        std::string diagnostic) {
  ConsumerPluginApplyResult out{};
  out.result = result;
  out.diagnostic = std::move(diagnostic);
  return out;
}

}  // namespace

ConsumerPluginRuntime::ConsumerPluginRuntime(
    RemotePluginMarketplace& marketplace,
    ConsumerPluginRuntimeBindings bindings)
    : marketplace_(marketplace), bindings_(std::move(bindings)) {}

std::vector<ConsumerPluginView> ConsumerPluginRuntime::browse(
    RemotePluginKind kind) const {
  std::vector<ConsumerPluginView> out;
  const ConsumerPluginPlan plan = bindings_.current_plan
      ? bindings_.current_plan() : ConsumerPluginPlan::free;
  for (const auto& entry : marketplace_.available(kind)) {
    ConsumerPluginView view{};
    view.plugin = entry;
    if (const auto installed = marketplace_.installed(entry.id))
      view.install_state = installed->state;
    view.requires_upgrade = entry.tier == RemotePluginTier::paid &&
                            plan == ConsumerPluginPlan::free;
    view.install_allowed = !view.requires_upgrade && !entry.revoked;
    view.apply_allowed = view.install_allowed &&
                         view.install_state == RemotePluginInstallState::installed;
    out.push_back(std::move(view));
  }
  return out;
}

bool ConsumerPluginRuntime::access_allowed(
    const RemotePluginCatalogEntry& entry,
    std::string& diagnostic) const {
  if (entry.revoked) {
    diagnostic = "plugin has been revoked";
    return false;
  }
  const ConsumerPluginPlan plan = bindings_.current_plan
      ? bindings_.current_plan() : ConsumerPluginPlan::free;
  if (entry.tier == RemotePluginTier::paid &&
      plan != ConsumerPluginPlan::paid) {
    diagnostic = "paid plugin requires a paid Digitor plan";
    return false;
  }
  diagnostic.clear();
  return true;
}

RemotePluginOperationResult ConsumerPluginRuntime::import_plugin(
    std::string_view plugin_id) {
  const auto entry = marketplace_.find(plugin_id);
  if (!entry) {
    RemotePluginOperationResult out{};
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "plugin is absent from the trusted catalog";
    return out;
  }
  std::string diagnostic;
  if (!access_allowed(*entry, diagnostic)) {
    RemotePluginOperationResult out{};
    out.result = DIGITOR_RESULT_UNSUPPORTED;
    out.diagnostic = std::move(diagnostic);
    return out;
  }
  if (entry->tier == RemotePluginTier::paid) {
    if (!bindings_.entitlement_for) {
      RemotePluginOperationResult out{};
      out.result = DIGITOR_RESULT_UNSUPPORTED;
      out.diagnostic = "paid plugin entitlement provider is unavailable";
      return out;
    }
    const auto entitlement = bindings_.entitlement_for(*entry);
    if (!entitlement) {
      RemotePluginOperationResult out{};
      out.result = DIGITOR_RESULT_UNSUPPORTED;
      out.diagnostic = "paid plugin entitlement is unavailable";
      return out;
    }
    return marketplace_.install(plugin_id, entitlement);
  }
  return marketplace_.install(plugin_id);
}

bool ConsumerPluginRuntime::validate_parameters(
    const RemotePluginCatalogEntry& entry,
    const std::unordered_map<std::string, double>& parameters,
    std::string& diagnostic) const {
  for (const auto& [id, value] : parameters) {
    const auto it = std::find_if(entry.parameters.begin(), entry.parameters.end(),
        [&id](const RemotePluginParameter& parameter) {
          return parameter.id == id;
        });
    if (it == entry.parameters.end() || value < it->minimum ||
        value > it->maximum) {
      diagnostic = "plugin parameter is unknown or outside its declared range: " + id;
      return false;
    }
  }
  diagnostic.clear();
  return true;
}

ConsumerPluginApplyResult ConsumerPluginRuntime::apply(
    std::string_view plugin_id, std::string_view instance_id,
    std::string_view project_or_clip_id,
    const std::unordered_map<std::string, double>& parameters,
    ConsumerPluginSurface surface) {
  if (instance_id.empty() || project_or_clip_id.empty())
    return apply_failure(DIGITOR_RESULT_INVALID_ARGUMENT,
                         "plugin instance and target identifiers are required");
  const auto entry = marketplace_.find(plugin_id);
  if (!entry)
    return apply_failure(DIGITOR_RESULT_INVALID_ARGUMENT,
                         "plugin is absent from the trusted catalog");
  std::string diagnostic;
  if (!access_allowed(*entry, diagnostic))
    return apply_failure(DIGITOR_RESULT_UNSUPPORTED, std::move(diagnostic));
  const auto installed = marketplace_.installed(plugin_id);
  if (!installed || installed->state != RemotePluginInstallState::installed ||
      installed->version != entry->version)
    return apply_failure(DIGITOR_RESULT_UNSUPPORTED,
                         "plugin is not installed at the catalog-pinned version");
  if (!validate_parameters(*entry, parameters, diagnostic))
    return apply_failure(DIGITOR_RESULT_INVALID_ARGUMENT, std::move(diagnostic));
  if (!bindings_.apply_instance)
    return apply_failure(DIGITOR_RESULT_INVALID_ARGUMENT,
                         "consumer plugin apply binding is unavailable");

  ConsumerPluginInstance value{};
  value.instance_id = std::string(instance_id);
  value.plugin_id = entry->id;
  value.plugin_version = entry->version;
  value.kind = entry->kind;
  value.tier = entry->tier;
  value.parameters = parameters;
  for (const auto& parameter : entry->parameters)
    value.parameters.try_emplace(parameter.id, parameter.default_value);

  if (!bindings_.apply_instance(value, surface, project_or_clip_id,
                                diagnostic))
    return apply_failure(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                         diagnostic.empty() ? "plugin application failed"
                                            : std::move(diagnostic));
  instances_[value.instance_id] = value;
  ConsumerPluginApplyResult out{};
  out.result = DIGITOR_RESULT_OK;
  out.diagnostic = "plugin applied";
  out.instance = value;
  return out;
}

DigitorResult ConsumerPluginRuntime::remove(
    std::string_view instance_id, std::string_view project_or_clip_id,
    ConsumerPluginSurface surface, std::string* diagnostic) {
  const auto it = instances_.find(std::string(instance_id));
  if (it == instances_.end() || project_or_clip_id.empty()) {
    if (diagnostic) *diagnostic = "plugin instance or target is invalid";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (bindings_.remove_instance)
    bindings_.remove_instance(instance_id, surface, project_or_clip_id);
  instances_.erase(it);
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

std::optional<ConsumerPluginInstance> ConsumerPluginRuntime::instance(
    std::string_view instance_id) const {
  const auto it = instances_.find(std::string(instance_id));
  return it == instances_.end() ? std::nullopt
                                : std::optional<ConsumerPluginInstance>(it->second);
}

std::vector<ConsumerPluginInstance> ConsumerPluginRuntime::instances() const {
  std::vector<ConsumerPluginInstance> out;
  out.reserve(instances_.size());
  for (const auto& [_, value] : instances_) out.push_back(value);
  return out;
}

}  // namespace digitor
