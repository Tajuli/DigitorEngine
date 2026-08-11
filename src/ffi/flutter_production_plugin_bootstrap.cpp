#include "digitor/flutter_production_plugin_bootstrap.hpp"
#include "digitor/flutter_production_provider_builder.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace digitor {
namespace {

struct PendingAttachment final {
  DigitorFlutterProductionPluginPlatform platform{
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS};
  const void* flutter_texture_registrar{};
  std::string implementation_identity;

  [[nodiscard]] bool matches(
      DigitorFlutterProductionPluginPlatform value_platform,
      const void* registrar) const noexcept {
    return platform == value_platform && flutter_texture_registrar == registrar;
  }
};

struct State {
  std::mutex mutex;
  std::array<FlutterProductionHostInputsFactory, 5> factories{};
  std::unique_ptr<RegisteredFlutterProductionHost> registration;
  std::optional<PendingAttachment> pending_attachment;
  const void* registrar{};
  DigitorFlutterProductionPluginPlatform platform{DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS};
  std::string last_diagnostic;
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

bool preview_backend_matches_platform(
    ProductionPlatform platform,
    DigitorNativeTextureBackend backend) noexcept {
  switch (platform) {
    case ProductionPlatform::windows:
      return backend == DIGITOR_NATIVE_TEXTURE_BACKEND_D3D12 ||
             backend == DIGITOR_NATIVE_TEXTURE_BACKEND_VULKAN;
    case ProductionPlatform::android:
      return backend == DIGITOR_NATIVE_TEXTURE_BACKEND_VULKAN ||
             backend == DIGITOR_NATIVE_TEXTURE_BACKEND_OPENGL_ES ||
             backend == DIGITOR_NATIVE_TEXTURE_BACKEND_ANDROID_HARDWARE_BUFFER;
    case ProductionPlatform::macos:
    case ProductionPlatform::ios:
      return backend == DIGITOR_NATIVE_TEXTURE_BACKEND_METAL;
  }
  return false;
}

DigitorResult register_from_factory_locked(
    State& s, DigitorFlutterProductionPluginPlatform platform,
    const void* registrar, const std::string& implementation_identity,
    std::string* diagnostic) noexcept {
  try {
    auto& factory = s.factories[static_cast<std::size_t>(platform)];
    if (!factory) {
      const std::string message =
          "production provider factory is not installed for the Flutter platform";
      s.last_diagnostic = message;
      if (diagnostic) *diagnostic = message;
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }

    FlutterProductionPluginAttachment resolved{};
    resolved.platform = platform;
    resolved.flutter_texture_registrar = registrar;
    resolved.implementation_identity = implementation_identity;
    std::string local;
    auto inputs = factory(resolved, local);
    if (!inputs) {
      if (local.empty()) {
        local = "production provider factory could not assemble complete host inputs";
      }
      s.last_diagnostic = local;
      if (diagnostic) *diagnostic = local;
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    auto registration =
        std::make_unique<RegisteredFlutterProductionHost>(std::move(*inputs));
    if (!registration->registered()) {
      const auto result = registration->result();
      local = "complete production provider inputs were rejected by the registered Flutter host";
      s.last_diagnostic = local;
      if (diagnostic) *diagnostic = local;
      return result;
    }

    s.platform = platform;
    s.registrar = registrar;
    s.registration = std::move(registration);
    s.pending_attachment.reset();
    s.last_diagnostic.clear();
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    s.last_diagnostic = "out of memory while registering Flutter production host";
    if (diagnostic) *diagnostic = s.last_diagnostic;
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    s.last_diagnostic = "production provider factory threw during host registration";
    if (diagnostic) *diagnostic = s.last_diagnostic;
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

}  // namespace

FlutterProductionProviderBuildValidation
validate_flutter_production_provider_build(
    DigitorFlutterProductionPluginPlatform platform,
    const FlutterProductionProviderBuild& build) noexcept {
  try {
    const auto expected = production_platform(platform);
    if (!expected) {
      return {DIGITOR_RESULT_INVALID_ARGUMENT,
              "invalid Flutter production provider platform"};
    }
    if (build.provider.platform != *expected ||
        build.platform_inputs.platform != *expected) {
      return {DIGITOR_RESULT_INVALID_ARGUMENT,
              "production provider platform does not match Flutter attachment"};
    }

    const auto provider_validation =
        validate_native_platform_provider(build.provider);
    if (!provider_validation) {
      return {provider_validation.result, provider_validation.diagnostic};
    }

    if (!build.decoder_factory) {
      return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
              "production decoder factory is required"};
    }
    if (!build.frame_resolver) {
      return {DIGITOR_RESULT_INVALID_ARGUMENT,
              "production timestamp/frame resolver is required"};
    }
    if (!build.texture_descriptor_builder) {
      return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
              "production GPU texture descriptor builder is required"};
    }
    if (!build.preview_target_binder) {
      return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
              "production Flutter preview target binder is required"};
    }
    if (build.fps_num <= 0 || build.fps_den <= 0) {
      return {DIGITOR_RESULT_INVALID_ARGUMENT,
              "production frame rate must be positive"};
    }
    if (build.video_bitrate <= 0) {
      return {DIGITOR_RESULT_INVALID_ARGUMENT,
              "production video bitrate must be positive"};
    }
    if (build.required_device_identity == 0 ||
        build.required_context_identity == 0) {
      return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
              "selected production device/context identity is required"};
    }

    const auto& preview = build.preview_capabilities;
    if (!preview.native_gpu_preview_available || preview.cpu_fallback_only ||
        preview.backend == DIGITOR_NATIVE_TEXTURE_BACKEND_NONE ||
        preview.backend == DIGITOR_NATIVE_TEXTURE_BACKEND_CPU_RGBA8 ||
        preview.handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_NONE ||
        preview.handle_type == DIGITOR_NATIVE_TEXTURE_HANDLE_CPU_POINTER) {
      return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,
              "production preview must expose a native GPU texture without CPU-only fallback"};
    }
    if (!preview_backend_matches_platform(*expected, preview.backend)) {
      return {DIGITOR_RESULT_INVALID_ARGUMENT,
              "production preview backend does not match the Flutter platform"};
    }

    return {DIGITOR_RESULT_OK, {}};
  } catch (...) {
    return {DIGITOR_RESULT_INTERNAL_ERROR,
            "failed to validate Flutter production provider build"};
  }
}

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
    if (s.pending_attachment && s.pending_attachment->platform == platform) {
      const auto pending = *s.pending_attachment;
      const auto result = register_from_factory_locked(
          s, pending.platform, pending.flutter_texture_registrar,
          pending.implementation_identity, diagnostic);
      if (result != DIGITOR_RESULT_OK) {
        // A factory is only considered installed when it can satisfy an
        // already-waiting Flutter attachment. This prevents app bootstrap from
        // observing a nominal builder that cannot construct the production host.
        s.factories[index] = {};
        return result;
      }
      return DIGITOR_RESULT_OK;
    }
    s.last_diagnostic.clear();
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

DigitorResult retry_flutter_production_host_registration(
    DigitorFlutterProductionPluginPlatform platform,
    std::string* diagnostic) noexcept {
  if (!valid_platform(platform)) {
    if (diagnostic) *diagnostic = "invalid Flutter production platform";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  try {
    auto& s = state();
    std::scoped_lock lock(s.mutex);

    // Nothing is waiting for this platform. Installing dependencies before the
    // Flutter plugin attaches is a valid startup order and needs no action.
    if (!s.pending_attachment || s.pending_attachment->platform != platform) {
      if (diagnostic) diagnostic->clear();
      return DIGITOR_RESULT_OK;
    }

    if (s.registration) {
      if (s.platform == platform &&
          s.registrar == s.pending_attachment->flutter_texture_registrar) {
        s.pending_attachment.reset();
        s.last_diagnostic.clear();
        if (diagnostic) diagnostic->clear();
        return DIGITOR_RESULT_OK;
      }
      const std::string message =
          "a different Flutter production host is already registered";
      s.last_diagnostic = message;
      if (diagnostic) *diagnostic = message;
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }

    auto& factory = s.factories[static_cast<std::size_t>(platform)];
    if (!factory) {
      const std::string message =
          "production provider factory is not installed for the Flutter platform";
      s.last_diagnostic = message;
      if (diagnostic) *diagnostic = message;
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }

    const auto pending = *s.pending_attachment;
    return register_from_factory_locked(
        s, pending.platform, pending.flutter_texture_registrar,
        pending.implementation_identity, diagnostic);
  } catch (const std::bad_alloc&) {
    if (diagnostic) *diagnostic =
        "out of memory while retrying Flutter production host registration";
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    if (diagnostic) *diagnostic =
        "failed to retry Flutter production host registration";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
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
        [platform, expected = *expected, factory = std::move(factory)](
            const FlutterProductionPluginAttachment& attachment,
            std::string& local) mutable
            -> std::optional<FlutterProductionHostAdapterInputs> {
          auto build = factory(attachment, local);
          if (!build) {
            if (local.empty()) local = "concrete production provider build unavailable";
            return std::nullopt;
          }
          const auto validation = validate_flutter_production_provider_build(
              platform, *build);
          if (!validation) {
            local = validation.diagnostic;
            return std::nullopt;
          }

          auto assembly = build->provider.create(std::move(build->platform_inputs));
          if (!assembly) {
            local = assembly.diagnostic.empty()
                        ? "production platform assembly failed"
                        : std::move(assembly.diagnostic);
            return std::nullopt;
          }
          if (assembly.platform != expected || !assembly.preview_session ||
              !assembly.timeline_binding || !assembly.timeline_binding->valid() ||
              !assembly.encoder_factory) {
            local =
                "production platform assembly is missing preview, timeline, or lazy encoder state";
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
    const auto platform =
        static_cast<DigitorFlutterProductionPluginPlatform>(attachment->platform);
    if (s.registration) {
      if (s.registrar == attachment->flutter_texture_registrar &&
          s.platform == platform) {
        s.last_diagnostic.clear();
        return DIGITOR_RESULT_OK;
      }
      s.last_diagnostic =
          "a different Flutter texture registrar already owns the production host";
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    if (s.pending_attachment &&
        !s.pending_attachment->matches(
            platform, attachment->flutter_texture_registrar)) {
      s.last_diagnostic =
          "a different Flutter texture registrar is already waiting for the production provider";
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }

    s.pending_attachment = digitor::PendingAttachment{
        platform, attachment->flutter_texture_registrar,
        attachment->implementation_identity};
    std::string diagnostic;
    return digitor::register_from_factory_locked(
        s, platform, attachment->flutter_texture_registrar,
        s.pending_attachment->implementation_identity, &diagnostic);
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
    if (!s.registration) {
      if (s.pending_attachment) {
        if (s.pending_attachment->flutter_texture_registrar !=
            flutter_texture_registrar) {
          s.last_diagnostic =
              "Flutter production detach registrar does not match pending attachment";
          return DIGITOR_RESULT_INVALID_ARGUMENT;
        }
        s.pending_attachment.reset();
      }
      s.last_diagnostic.clear();
      return DIGITOR_RESULT_OK;
    }
    if (s.registrar != flutter_texture_registrar) return DIGITOR_RESULT_INVALID_ARGUMENT;
    const auto result = s.registration->unregister();
    if (result != DIGITOR_RESULT_OK) {
      s.last_diagnostic =
          "production host cannot detach while a registered session is still active";
      return result;
    }
    s.registration.reset();
    s.registrar = nullptr;
    s.last_diagnostic.clear();
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

const char* digitor_flutter_production_plugin_last_error(void) {
  thread_local std::string snapshot;
  try {
    auto& s = digitor::state();
    std::scoped_lock lock(s.mutex);
    snapshot = s.last_diagnostic;
  } catch (...) {
    snapshot = "failed to query Flutter production bootstrap diagnostic";
  }
  return snapshot.c_str();
}

}  // extern "C"
