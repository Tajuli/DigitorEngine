#pragma once

#include "digitor/flutter_production_host_adapter.hpp"
#include "digitor/flutter_production_plugin_c_api.h"

#include <functional>
#include <optional>
#include <string>

namespace digitor {

struct FlutterProductionPluginAttachment final {
  DigitorFlutterProductionPluginPlatform platform{DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS};
  const void* flutter_texture_registrar{};
  std::string implementation_identity;
};

using FlutterProductionHostInputsFactory = std::function<std::optional<FlutterProductionHostAdapterInputs>(
    const FlutterProductionPluginAttachment&, std::string& diagnostic)>;

DigitorResult install_flutter_production_host_inputs_factory(
    DigitorFlutterProductionPluginPlatform platform,
    FlutterProductionHostInputsFactory factory,
    std::string* diagnostic = nullptr) noexcept;

DigitorResult clear_flutter_production_host_inputs_factory(
    DigitorFlutterProductionPluginPlatform platform) noexcept;

// Re-evaluates a previously pending Flutter attachment against the currently
// installed platform host-input factory. This is the late-binding bridge used
// when the engine runtime is already installed and concrete platform
// dependencies (decoder/timeline/presenter/encoder bindings) arrive afterward.
// A matching pending attachment is consumed only after a complete host is
// successfully registered; otherwise it remains pending for a later retry.
DigitorResult retry_flutter_production_host_registration(
    DigitorFlutterProductionPluginPlatform platform,
    std::string* diagnostic = nullptr) noexcept;

}  // namespace digitor
