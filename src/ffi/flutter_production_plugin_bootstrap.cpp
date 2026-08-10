#include "digitor/flutter_production_plugin_bootstrap.hpp"
#include "digitor/flutter_production_provider_builder.hpp"

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

std::optional<ProductionPlatform> production_platform(
    DigitorFlutterProductionPluginPlatform platform) noexcept {
  switch (platform) {
    case DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS:
      return ProductionPlatform::windows;
    case DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ANDROID:
      return ProductionPlatform::android;
    case DIGITOR_FLUTTER_PRODUCTION_PLUGIN_MACOS:
      return ProductionPlatform::macos;
    case DIGITOR_FLUTTER_PRODUCTION_PLUGIN_IOS:
      return ProductionPlatform::ios;
  }
  return std::nullopt;
}

bool host_inputs_complete(const FlutterProductionProviderBuild& build) noexcept {
  // Attachment readiness is preview-only. Export backend/snapshot validation
  // is deferred until frozen V2 export starts.
  return build.decoder_factory && build.frame_resolver &&
         build.texture_descriptor_builder && build.preview_target_binder &&
         build.fps_num > 0 && build.fps_den > 0 && build.video_bitrate > 0 &&
         build.required_device_identity != 0 &&
         build.required_context_identity != 0;
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

DigitorResult install_flutter_production_provider_builder(
    DigitorFlutterProductionPluginPlatform platform,
    FlutterProductionProviderBuildFactory factory,
    std::string* diagnostic) noexcept {
  try {
    const auto expected = production_platform(platform);
    if (!expected || !factory) {
      if (diagnostic) *diagnostic = "valid production provider builder is required";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }

    return install_flutter_production_host_inputs_factory(
        platform,
        [expected = *expected, factory = std::move(factory)](
            const FlutterProductionPluginAttachment& attachment,
            std::string& local) mutable
            -> std::optional<FlutterProductionHostAdapterInputs> {
          auto build = factory(attachment, local);
          if (!build) {
            if (local.empty()) local = "concrete production provider build unavailable";
            return std::nullopt;
          }
          if (build->provider.platform != expected ||
              build->platform_inputs.platform != expected) {
            local = "production provider platform does not match Flutter attachment";
            return std::nullopt;
          }
          const auto provider_validation =
              validate_native_platform_provider(build->provider);
          if (!provider_validation) {
            local = provider_validation.diagnostic;
            return std::nullopt;
          }
          if (!host_inputs_complete(*build)) {
            local = "production provider host inputs are incomplete";
            return std::nullopt;
          }

          auto assembly = build->provider.create(std::move(build->platform_inputs));
          if (!assembly) {
            local = assembly.diagnostic.empty()
                        ? "production platform assembly failed"
                        : std::move(assembly.diagnostic);
            return std::nullopt;
          }

          FlutterProductionHostAdapterInputs out{};
          out.decoder_factory = std::move(build->decoder_factory);
          out.frame_resolver = std::move(build->frame_resolver);
          out.preview_session = std::move(assembly.preview_session);
          out.encoder_callbacks = std::move(assembly.encoder_callbacks);
          out.encoder_factory = std::move(assembly.encoder_factory);
          out.texture_descriptor_builder =
              std::move(build->texture_descriptor_builder);
          out.preview_target_binder = std::move(build->preview_target_binder);
          out.preview_capabilities = build->preview_capabilities;
          out.encoder_backend = build->encoder_backend;
          out.fps_num = build->fps_num;
          out.fps_den = build->fps_den;
          out.video_bitrate = build->video_bitrate;
          out.required_device_identity = build->required_device_identity;
          out.required_context_identity = build->required_context_identity;
          local.clear();
          return out;
        },
        diagnostic);
  } catch (...) {
    if (diagnostic) *diagnostic = "failed to install production provider builder";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

DigitorResult uninstall_flutter_production_provider_builder(
    DigitorFlutterProductionPluginPlatform platform) noexcept {
  return clear_flutter_production_host_inputs_factory(platform);
}

bool flutter_production_provider_builder_installed(
    DigitorFlutterProductionPluginPlatform platform) noexcept {
  if (!valid_platform(platform)) return false;
  auto& s = state();
  std::scoped_lock lock(s.mutex);
  return static_cast<bool>(s.factories[static_cast<std::size_t>(platform)]);
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
    const auto result = s.registration->unregister();
    if (result != DIGITOR_RESULT_OK) return result;
    s.registration.reset();
    s.registrar = nullptr;
    return DIGITOR_RESULT_OK;
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
