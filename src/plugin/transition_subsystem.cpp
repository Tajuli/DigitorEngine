#include "digitor/transition_subsystem.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace digitor {
namespace {

constexpr std::array<std::string_view, 4> kBuiltinIds{
    "builtin.cross-dissolve",
    "builtin.dip-to-color",
    "builtin.wipe",
    "builtin.slide",
};

TransitionType type_for_id(std::string_view id) noexcept {
  if (id == kBuiltinIds[1]) return TransitionType::dip_to_color;
  if (id == kBuiltinIds[2]) return TransitionType::wipe;
  if (id == kBuiltinIds[3]) return TransitionType::slide;
  return TransitionType::cross_dissolve;
}

double parameter(const PluginTransitionInstance& instance,
                 std::string_view id,
                 double fallback) noexcept {
  const auto found = instance.parameters.find(std::string(id));
  return found == instance.parameters.end() ? fallback : found->second;
}

bool valid_descriptor(const TransitionDescriptor& descriptor) noexcept {
  return !descriptor.id.empty() && !descriptor.display_name.empty() &&
         !descriptor.version.empty();
}

}  // namespace

bool is_builtin_transition_id(std::string_view id) noexcept {
  return std::find(kBuiltinIds.begin(), kBuiltinIds.end(), id) != kBuiltinIds.end();
}

TransitionSettings builtin_transition_settings(
    const PluginTransitionInstance& instance,
    std::string* diagnostic) noexcept {
  TransitionSettings settings;
  settings.type = type_for_id(instance.plugin_id);
  settings.progress = static_cast<float>(instance.progress);
  settings.softness = static_cast<float>(parameter(instance, "softness", 0.02));
  settings.direction = static_cast<TransitionDirection>(
      static_cast<std::uint32_t>(parameter(instance, "direction", 0.0)));
  settings.dip_r = static_cast<float>(parameter(instance, "dip_r", 0.0));
  settings.dip_g = static_cast<float>(parameter(instance, "dip_g", 0.0));
  settings.dip_b = static_cast<float>(parameter(instance, "dip_b", 0.0));
  settings.dip_a = static_cast<float>(parameter(instance, "dip_a", 1.0));
  settings.ease_in_out = parameter(instance, "ease_in_out", 1.0) != 0.0;

  const auto direction = static_cast<std::uint32_t>(settings.direction);
  const bool valid = is_builtin_transition_id(instance.plugin_id) &&
                     std::isfinite(settings.progress) &&
                     settings.progress >= 0.0f && settings.progress <= 1.0f &&
                     std::isfinite(settings.softness) &&
                     settings.softness >= 0.0f && settings.softness <= 1.0f &&
                     direction <= static_cast<std::uint32_t>(TransitionDirection::down);
  if (!valid && diagnostic) {
    *diagnostic = "built-in transition parameters are invalid";
  } else if (diagnostic) {
    diagnostic->clear();
  }
  if (!valid) settings.progress = -1.0f;
  return settings;
}

TransitionSubsystem::TransitionSubsystem(TransitionSubsystemBindings bindings)
    : bindings_(std::move(bindings)),
      descriptors_{{std::string(kBuiltinIds[0]), "Cross Dissolve", "1", TransitionProviderKind::built_in},
                   {std::string(kBuiltinIds[1]), "Dip to Color", "1", TransitionProviderKind::built_in},
                   {std::string(kBuiltinIds[2]), "Directional Wipe", "1", TransitionProviderKind::built_in},
                   {std::string(kBuiltinIds[3]), "Directional Slide", "1", TransitionProviderKind::built_in}} {}

bool TransitionSubsystem::register_plugin(TransitionDescriptor descriptor,
                                          std::string* diagnostic) {
  if (!valid_descriptor(descriptor) || descriptor.provider != TransitionProviderKind::plugin ||
      is_builtin_transition_id(descriptor.id) || find(descriptor.id)) {
    if (diagnostic) *diagnostic = "transition plugin descriptor is invalid or conflicts with an existing id";
    return false;
  }
  descriptors_.push_back(std::move(descriptor));
  if (diagnostic) diagnostic->clear();
  return true;
}

const std::vector<TransitionDescriptor>& TransitionSubsystem::descriptors() const noexcept {
  return descriptors_;
}

const TransitionDescriptor* TransitionSubsystem::find(std::string_view id) const noexcept {
  const auto found = std::find_if(descriptors_.begin(), descriptors_.end(),
                                  [id](const auto& descriptor) { return descriptor.id == id; });
  return found == descriptors_.end() ? nullptr : &*found;
}

TransitionResult TransitionSubsystem::render_reference(
    std::string_view transition_id,
    const TransitionFrame& outgoing,
    const TransitionFrame& incoming,
    TransitionFrame& output,
    const TransitionSettings& settings) const {
  if (!is_builtin_transition_id(transition_id)) return {};
  auto resolved = settings;
  resolved.type = type_for_id(transition_id);
  return apply_transition_reference(outgoing, incoming, output, resolved);
}

DigitorResult TransitionSubsystem::dispatch(const PluginTransitionRequest& request,
                                            std::string* diagnostic) const noexcept {
  std::string local;
  const auto* descriptor = find(request.instance.plugin_id);
  if (!descriptor) {
    if (diagnostic) *diagnostic = "transition id is not registered";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (!validate_plugin_transition_request(request, bindings_.selected_backend, local)) {
    if (diagnostic) *diagnostic = local;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  if (descriptor->provider == TransitionProviderKind::built_in) {
    const auto settings = builtin_transition_settings(request.instance, &local);
    if (settings.progress < 0.0f) {
      if (diagnostic) *diagnostic = local;
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    if (!bindings_.record_built_in) {
      if (diagnostic) *diagnostic = "built-in transition GPU recorder is unavailable";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    const auto result = bindings_.record_built_in(request, settings, local);
    if (diagnostic) *diagnostic = result == DIGITOR_RESULT_OK ? std::string{} : local;
    return result;
  }

  PluginTransitionRuntime runtime({bindings_.selected_backend, bindings_.record_plugin});
  return runtime.dispatch(request, diagnostic);
}

}  // namespace digitor

extern "C" std::uint32_t digitor_builtin_transition_count(void) {
  return 4u;
}

extern "C" std::uint32_t digitor_builtin_transition_id(std::uint32_t index,
                                                        char* output,
                                                        std::uint32_t capacity) {
  constexpr std::array<std::string_view, 4> ids{
      "builtin.cross-dissolve", "builtin.dip-to-color", "builtin.wipe", "builtin.slide"};
  if (index >= ids.size() || !output || capacity == 0u) return 1u;
  const auto id = ids[index];
  if (capacity <= id.size()) return 2u;
  std::memcpy(output, id.data(), id.size());
  output[id.size()] = '\0';
  return 0u;
}
