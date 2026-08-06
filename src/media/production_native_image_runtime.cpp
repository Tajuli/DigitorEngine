#include "digitor/production_native_image_runtime.hpp"

#include <array>
#include <mutex>
#include <utility>

namespace digitor {
namespace {

constexpr std::size_t platform_index(NativeStillPlatform platform) noexcept {
  return static_cast<std::size_t>(platform);
}

struct Registry {
  std::mutex mutex;
  std::array<NativeStillImageServiceFactory, 3> factories;
};

Registry& registry() {
  static Registry value;
  return value;
}

}  // namespace

void register_native_still_image_services(
    NativeStillPlatform platform,
    NativeStillImageServiceFactory factory) {
  auto& value = registry();
  std::lock_guard lock(value.mutex);
  value.factories.at(platform_index(platform)) = std::move(factory);
}

void clear_native_still_image_services_for_test() noexcept {
  auto& value = registry();
  std::lock_guard lock(value.mutex);
  for (auto& factory : value.factories) factory = {};
}

bool native_still_image_services_registered(
    NativeStillPlatform platform) noexcept {
  auto& value = registry();
  std::lock_guard lock(value.mutex);
  return static_cast<bool>(value.factories.at(platform_index(platform)));
}

ProductionNativeImageRuntimeResult make_production_native_image_runtime(
    ProductionNativeImageRuntimeConfig config) {
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
    auto& value = registry();
    std::lock_guard lock(value.mutex);
    factory = value.factories.at(platform_index(config.platform));
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
