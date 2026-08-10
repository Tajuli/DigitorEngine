#include "platform/windows/windows_engine_production.hpp"

#include <cstdint>
#include <mutex>
#include <utility>

namespace digitor {
namespace {

struct DependencyFactoryState final {
  std::mutex mutex;
  WindowsEngineProductionDependenciesFactory factory;
};

DependencyFactoryState& dependency_factory_state() {
  static DependencyFactoryState state;
  return state;
}

const void* context_pointer(const BackendProductionCapability& backend) noexcept {
  return backend.frame_context_identity;
}

bool complete_encoder(const WindowsHardwareEncoderHost& host) noexcept {
  return host.open && host.submit && host.drain && host.finalize_atomic &&
         host.cancel && host.qualification;
}

bool complete_dependencies(const WindowsEngineProductionDependencies& value) noexcept {
  return value.timeline.create_target && value.timeline.execute_effects &&
         value.timeline.composite_layer && value.timeline.frame_evictable &&
         complete_encoder(value.encoder) && value.decoder_factory &&
         value.frame_resolver && value.texture_descriptor_builder &&
         value.preview_target_binder && value.flutter_present &&
         value.fps_num > 0 && value.fps_den > 0 && value.video_bitrate > 0 &&
         !value.package_identity.empty() && !value.build_identity.empty();
}

bool capability_resources_complete(const BackendProductionCapability& backend,
                                   std::string& diagnostic) noexcept {
  if (!backend.valid() || !backend.frame_context_identity) {
    diagnostic = "selected Windows backend has no live production capability";
    return false;
  }
  if (backend.backend == DIGITOR_RENDERER_D3D12) {
    const auto* d3d12 = std::get_if<D3D12ProductionResources>(&backend.resources);
    if (!d3d12 || !d3d12->device || !d3d12->command_queue) {
      diagnostic = "D3D12 production capability is missing device/queue ownership";
      return false;
    }
    return true;
  }
  if (backend.backend == DIGITOR_RENDERER_VULKAN) {
    const auto* vk = std::get_if<VulkanProductionResources>(&backend.resources);
    if (!vk || !vk->instance || !vk->physical_device || !vk->device ||
        !vk->queue) {
      diagnostic = "Vulkan production capability is missing native device/queue ownership";
      return false;
    }
    return true;
  }
  diagnostic = "Windows production requires selected Vulkan or D3D12 backend";
  return false;
}

}  // namespace

WindowsEngineProductionBuildResult assemble_windows_engine_production_build(
    const BackendProductionCapability& backend,
    const FlutterProductionPluginAttachment& attachment,
    WindowsEngineProductionDependencies dependencies) noexcept {
  WindowsEngineProductionBuildResult out{};
  try {
    if (attachment.platform != DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS ||
        !attachment.flutter_texture_registrar ||
        attachment.implementation_identity.empty()) {
      out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
      out.diagnostic = "valid Windows Flutter production attachment is required";
      return out;
    }
    if (!capability_resources_complete(backend, out.diagnostic)) {
      out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      return out;
    }
    if (!complete_dependencies(dependencies)) {
      out.result = DIGITOR_RESULT_NOT_INITIALIZED;
      out.diagnostic = "engine-owned Windows production dependencies are incomplete";
      return out;
    }

    const auto* frame_context = context_pointer(backend);
    if (dependencies.timeline.backend != backend.backend ||
        dependencies.timeline.context_identity != frame_context) {
      out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      out.diagnostic =
          "Windows timeline is not bound to the selected backend generation";
      return out;
    }
    if (dependencies.preview_capabilities.native_gpu_preview_available == 0 ||
        dependencies.preview_capabilities.backend ==
            DIGITOR_NATIVE_TEXTURE_BACKEND_NONE ||
        dependencies.preview_capabilities.handle_type ==
            DIGITOR_NATIVE_TEXTURE_HANDLE_NONE ||
        dependencies.preview_capabilities.handle_type ==
            DIGITOR_NATIVE_TEXTURE_HANDLE_CPU_POINTER) {
      out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      out.diagnostic = "Windows native preview capability is not GPU production-ready";
      return out;
    }

    WindowsNativeProviderBindings bindings{};
    bindings.timeline = dependencies.timeline;
    bindings.flutter.flutter_texture_registrar =
        attachment.flutter_texture_registrar;
    bindings.flutter.implementation_identity = attachment.implementation_identity;
    bindings.flutter.attached = [] { return true; };
    bindings.flutter.present =
        [attachment, presenter = dependencies.flutter_present](
            const ProcessedGpuFramePtr& frame, std::uint64_t generation) mutable {
          std::string diagnostic;
          return presenter(attachment, frame, generation, diagnostic);
        };
    bindings.encoder = dependencies.encoder;
    bindings.vulkan_interop = dependencies.vulkan_interop;
    bindings.capabilities = dependencies.capabilities;
    bindings.device_identity = frame_context;
    bindings.package_identity = dependencies.package_identity;
    bindings.build_identity = dependencies.build_identity;

#if !defined(_WIN32)
    out.result = DIGITOR_RESULT_UNSUPPORTED;
    out.diagnostic = "Windows production assembly can only be completed on Windows";
    return out;
#else
    auto provider = create_windows_native_provider(std::move(bindings));
    if (!provider) {
      out.result = provider.result;
      out.diagnostic = provider.diagnostic;
      return out;
    }

    FlutterProductionProviderBuild build{};
    build.provider = std::move(provider.provider);
    build.platform_inputs.platform = ProductionPlatform::windows;
    build.platform_inputs.timeline = dependencies.timeline;
    build.decoder_factory = std::move(dependencies.decoder_factory);
    build.frame_resolver = std::move(dependencies.frame_resolver);
    build.texture_descriptor_builder =
        std::move(dependencies.texture_descriptor_builder);
    build.preview_target_binder = std::move(dependencies.preview_target_binder);
    build.preview_capabilities = dependencies.preview_capabilities;
    build.encoder_backend = dependencies.encoder_backend;
    build.fps_num = dependencies.fps_num;
    build.fps_den = dependencies.fps_den;
    build.video_bitrate = dependencies.video_bitrate;
    build.required_device_identity =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(frame_context));
    build.required_context_identity = backend.context_identity;

    out.build = std::move(build);
    out.result = DIGITOR_RESULT_OK;
    return out;
#endif
  } catch (const std::bad_alloc&) {
    out.result = DIGITOR_RESULT_OUT_OF_MEMORY;
    out.diagnostic = "out of memory assembling Windows production provider";
  } catch (...) {
    out.result = DIGITOR_RESULT_INTERNAL_ERROR;
    out.diagnostic = "unexpected Windows production provider assembly failure";
  }
  return out;
}

DigitorResult install_windows_engine_production_dependencies_factory(
    WindowsEngineProductionDependenciesFactory factory,
    std::string* diagnostic) noexcept {
  if (!factory) {
    if (diagnostic) *diagnostic = "Windows production dependency factory is required";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  try {
    auto& state = dependency_factory_state();
    std::scoped_lock lock(state.mutex);
    if (state.factory) {
      if (diagnostic) *diagnostic = "Windows production dependency factory already installed";
      return DIGITOR_RESULT_RESOURCE_IN_USE;
    }
    state.factory = std::move(factory);
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  } catch (...) {
    if (diagnostic) *diagnostic = "failed to install Windows production dependency factory";
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

DigitorResult clear_windows_engine_production_dependencies_factory() noexcept {
  auto& state = dependency_factory_state();
  std::scoped_lock lock(state.mutex);
  state.factory = {};
  return DIGITOR_RESULT_OK;
}

std::unique_ptr<ProductionIntegrationRuntime>
install_windows_engine_production_runtime(
    const BackendProductionCapability& backend, std::string* diagnostic) noexcept {
  std::string capability_diagnostic;
  if (!capability_resources_complete(backend, capability_diagnostic)) {
    if (diagnostic) *diagnostic = std::move(capability_diagnostic);
    return {};
  }

  return ProductionIntegrationRuntime::install(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS,
      [backend](const FlutterProductionPluginAttachment& attachment,
                std::string& local)
          -> std::optional<FlutterProductionProviderBuild> {
        WindowsEngineProductionDependenciesFactory factory;
        {
          auto& state = dependency_factory_state();
          std::scoped_lock lock(state.mutex);
          factory = state.factory;
        }
        if (!factory) {
          local = "engine-owned Windows production dependencies are not installed";
          return std::nullopt;
        }
        auto dependencies = factory(backend, attachment, local);
        if (!dependencies) {
          if (local.empty())
            local = "engine-owned Windows production dependencies are unavailable";
          return std::nullopt;
        }
        auto assembled = assemble_windows_engine_production_build(
            backend, attachment, std::move(*dependencies));
        if (!assembled) {
          local = assembled.diagnostic;
          return std::nullopt;
        }
        local.clear();
        return std::move(assembled.build);
      },
      diagnostic);
}

}  // namespace digitor
