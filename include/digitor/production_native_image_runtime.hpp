#pragma once

#include "digitor/image_editor_runtime.hpp"
#include "digitor/native_still_image_host.hpp"

#include <array>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace digitor {

using NativeStillImageServiceFactory =
    std::function<NativeStillImageServices(DigitorRendererBackend,
                                           const void* context_identity,
                                           const std::string& device_identity)>;

struct ProductionNativeImageRuntimeConfig {
  NativeStillPlatform platform{NativeStillPlatform::windows};
  DigitorRendererBackend selected_backend{DIGITOR_RENDERER_CPU};
  const void* context_identity{};
  std::string device_identity;
  NativeStillImageLimits limits{};
  NativeStillImageProgress progress{};
  std::function<DigitorResult(const ImageEditorCpuProcessRequest&,
                              RenderVideoFrame&, std::string&)>
      process_cpu;
};

struct ProductionNativeImageRuntimeResult {
  DigitorResult result{DIGITOR_RESULT_OK};
  std::string diagnostic;
  ImageEditorRuntimeConfig runtime_config;

  [[nodiscard]] explicit operator bool() const noexcept {
    return result == DIGITOR_RESULT_OK;
  }
};

namespace native_image_runtime_detail {

inline constexpr std::size_t platform_index(
    NativeStillPlatform platform) noexcept {
  return static_cast<std::size_t>(platform);
}

struct Registry {
  std::mutex mutex;
  std::array<NativeStillImageServiceFactory, 3> factories;
};

inline Registry& registry() {
  static Registry value;
  return value;
}

}  // namespace native_image_runtime_detail

inline void register_native_still_image_services(
    NativeStillPlatform platform,
    NativeStillImageServiceFactory factory) {
  auto& value = native_image_runtime_detail::registry();
  std::lock_guard lock(value.mutex);
  value.factories.at(native_image_runtime_detail::platform_index(platform)) =
      std::move(factory);
}

inline void clear_native_still_image_services_for_test() noexcept {
  auto& value = native_image_runtime_detail::registry();
  std::lock_guard lock(value.mutex);
  for (auto& factory : value.factories) factory = {};
}

[[nodiscard]] inline bool native_still_image_services_registered(
    NativeStillPlatform platform) noexcept {
  auto& value = native_image_runtime_detail::registry();
  std::lock_guard lock(value.mutex);
  return static_cast<bool>(value.factories.at(
      native_image_runtime_detail::platform_index(platform)));
}

// Builds a locked image-editor configuration. A GPU backend requires a complete
// registered native provider. CPU is allowed only when CPU was selected before
// the image session is created; GPU provider failure never falls back silently.
[[nodiscard]] inline ProductionNativeImageRuntimeResult
make_production_native_image_runtime(ProductionNativeImageRuntimeConfig config) {
  ProductionNativeImageRuntimeResult result;
  result.runtime_config.process_cpu = std::move(config.process_cpu);

  if (config.selected_backend == DIGITOR_RENDERER_CPU) {
    if (!result.runtime_config.process_cpu) {
      result.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      result.diagnostic = "CPU image processing host is unavailable";
      return result;
    }
    result.runtime_config.allow_cpu_fallback = true;
    return result;
  }

  if (config.selected_backend == DIGITOR_RENDERER_AUTO) {
    result.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    result.diagnostic =
        "image runtime requires the engine-selected concrete backend";
    return result;
  }
  if (!native_still_backend_matches_platform(config.platform,
                                              config.selected_backend)) {
    result.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    result.diagnostic = "selected image backend does not match the platform";
    return result;
  }
  if (!config.context_identity || config.device_identity.empty()) {
    result.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    result.diagnostic = "GPU image runtime identity is incomplete";
    return result;
  }

  NativeStillImageServiceFactory factory;
  {
    auto& value = native_image_runtime_detail::registry();
    std::lock_guard lock(value.mutex);
    factory = value.factories.at(
        native_image_runtime_detail::platform_index(config.platform));
  }
  if (!factory) {
    result.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    result.diagnostic = "native image codec/GPU provider is not registered";
    return result;
  }

  NativeStillImageHostConfig host_config;
  host_config.platform = config.platform;
  host_config.backend = config.selected_backend;
  host_config.context_identity = config.context_identity;
  host_config.device_identity = std::move(config.device_identity);
  host_config.limits = config.limits;
  host_config.progress = std::move(config.progress);
  host_config.services = factory(config.selected_backend,
                                 config.context_identity,
                                 host_config.device_identity);

  auto host = make_native_still_image_host(std::move(host_config));
  if (!host) {
    result.result = host.result;
    result.diagnostic = std::move(host.diagnostic);
    return result;
  }

  result.runtime_config.gpu_host = std::move(host.host);
  result.runtime_config.allow_cpu_fallback = false;
  return result;
}

}  // namespace digitor
