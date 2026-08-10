#include "digitor/flutter_production_plugin_bootstrap.hpp"

#include <array>
#include <memory>
#include <mutex>

namespace digitor {
namespace {

struct State {
  std::mutex mutex;
  std::array<FlutterProductionHostInputsFactory, 5> factories{};
  std::unique_ptr<RegisteredFlutterProductionHost> registration;
  const void* registrar{};
  DigitorFlutterProductionPluginPlatform platform{DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS};
};

State& state() {
  static State value;
  return value;
}

bool valid_platform(std::uint32_t value) noexcept {
  return value >= DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS &&
         value <= DIGITOR_FLUTTER_PRODUCTION_PLUGIN_IOS;
}

}  // namespace

DigitorResult install_flutter_production_host_inputs_factory(
    DigitorFlutterProductionPluginPlatform platform,
    FlutterProductionHostInputsFactory factory,
    std::string* diagnostic) noexcept {
  try {
    if (!valid_platform(platform) || !factory) {
      if (diagnostic) *diagnostic = "valid platform production-host factory is required";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    auto& s = state();
    std::scoped_lock lock(s.mutex);
    const auto index = static_cast<std::size_t>(platform);
    if (s.factories[index]) {
      if (diagnostic) *diagnostic = "platform production-host factory already installed";
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    s.factories[index] = std::move(factory);
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  } catch (...) {
    if (diagnostic) *diagnostic = "failed to install production-host factory";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

DigitorResult clear_flutter_production_host_inputs_factory(
    DigitorFlutterProductionPluginPlatform platform) noexcept {
  if (!valid_platform(platform)) return DIGITOR_RESULT_INVALID_ARGUMENT;
  auto& s = state();
  std::scoped_lock lock(s.mutex);
  if (s.registration && s.platform == platform) return DIGITOR_RESULT_RESOURCE_IN_USE;
  s.factories[static_cast<std::size_t>(platform)] = {};
  return DIGITOR_RESULT_OK;
}

}  // namespace digitor

extern "C" {

DigitorResult digitor_flutter_production_plugin_attach(
    const DigitorFlutterProductionPluginAttachment* attachment) {
  if (!attachment ||
      attachment->struct_size < sizeof(DigitorFlutterProductionPluginAttachment) ||
      attachment->api_version != DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ATTACHMENT_VERSION ||
      !digitor::valid_platform(attachment->platform) ||
      !attachment->flutter_texture_registrar ||
      !attachment->implementation_identity || attachment->implementation_identity[0] == '\0') {
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }

  try {
    auto& s = digitor::state();
    std::scoped_lock lock(s.mutex);
    if (s.registration) {
      return s.registrar == attachment->flutter_texture_registrar
                 ? DIGITOR_RESULT_OK
                 : DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    const auto platform = static_cast<DigitorFlutterProductionPluginPlatform>(attachment->platform);
    auto& factory = s.factories[static_cast<std::size_t>(platform)];
    if (!factory) return DIGITOR_RESULT_NOT_INITIALIZED;

    digitor::FlutterProductionPluginAttachment resolved{};
    resolved.platform = platform;
    resolved.flutter_texture_registrar = attachment->flutter_texture_registrar;
    resolved.implementation_identity = attachment->implementation_identity;
    std::string diagnostic;
    auto inputs = factory(resolved, diagnostic);
    if (!inputs) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    auto registration = std::make_unique<digitor::RegisteredFlutterProductionHost>(
        std::move(*inputs));
    if (!registration->registered()) return registration->result();
    s.platform = platform;
    s.registrar = attachment->flutter_texture_registrar;
    s.registration = std::move(registration);
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

DigitorResult digitor_flutter_production_plugin_detach(
    const void* flutter_texture_registrar) {
  if (!flutter_texture_registrar) return DIGITOR_RESULT_INVALID_ARGUMENT;
  try {
    auto& s = digitor::state();
    std::scoped_lock lock(s.mutex);
    if (!s.registration) return DIGITOR_RESULT_OK;
    if (s.registrar != flutter_texture_registrar) return DIGITOR_RESULT_INVALID_ARGUMENT;
    s.registration.reset();
    s.registrar = nullptr;
    return digitor_flutter_production_host_registered() == 0
               ? DIGITOR_RESULT_OK
               : DIGITOR_RESULT_RESOURCE_IN_USE;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

uint8_t digitor_flutter_production_plugin_attached(void) {
  auto& s = digitor::state();
  std::scoped_lock lock(s.mutex);
  return s.registration && s.registration->registered() ? 1u : 0u;
}

}  // extern "C"
