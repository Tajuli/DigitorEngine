#include "digitor/production_native_image_runtime.hpp"

#include <array>
#include <mutex>
#include <utility>

namespace digitor {
namespace {

std::mutex registry_mutex;
std::array<NativeStillImageServiceFactory, 3> registry;

std::size_t platform_index(NativeStillPlatform platform) noexcept {
  return static_cast<std::size_t>(platform);
}

bool services_complete(const NativeStillImageServices& services) noexcept {
  return services.decode_to_gpu && services.resize_gpu &&
         services.process_graph && services.encode_from_gpu;
}

#if defined(_WIN32)
std::unique_ptr<NativeImageCodec> create_windows_wic_image_codec() noexcept {
  try { return std::make_unique<WicImageCodec>(); } catch (...) { return {}; }
}
#elif defined(__ANDROID__)
std::unique_ptr<NativeImageCodec> create_android_image_codec() noexcept {
  try { return std::make_unique<AndroidImageCodec>(); } catch (...) { return {}; }
}
#endif

}  // namespace

void register_native_still_image_services(
    NativeStillPlatform platform, NativeStillImageServiceFactory factory) {
  std::lock_guard lock(registry_mutex);
  registry[platform_index(platform)] = std::move(factory);
}

void clear_native_still_image_services_for_test() noexcept {
  std::lock_guard lock(registry_mutex);
  registry = {};
}

bool native_still_image_services_registered(
    NativeStillPlatform platform) noexcept {
  std::lock_guard lock(registry_mutex);
  return static_cast<bool>(registry[platform_index(platform)]);
}

ProductionNativeImageRuntimeResult make_production_native_image_runtime(
    ProductionNativeImageRuntimeConfig config) {
  ProductionNativeImageRuntimeResult result;
  result.runtime_config.process_cpu = std::move(config.process_cpu);

  if (config.selected_backend == DIGITOR_RENDERER_CPU) {
    if (!result.runtime_config.process_cpu) {
      result.result = DIGITOR_RESULT_INVALID_ARGUMENT;
      result.diagnostic = "CPU image runtime requires a CPU graph executor";
      return result;
    }
    result.runtime_config.allow_cpu_fallback = true;
    result.result = DIGITOR_RESULT_OK;
    return result;
  }

  if (!native_still_backend_matches_platform(config.platform,
                                              config.selected_backend)) {
    result.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    result.diagnostic = "selected GPU backend does not match image platform";
    return result;
  }
  if (!config.context_identity || config.device_identity.empty()) {
    result.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    result.diagnostic = "GPU image runtime requires context and device identity";
    return result;
  }

  NativeStillImageServiceFactory factory;
  {
    std::lock_guard lock(registry_mutex);
    factory = registry[platform_index(config.platform)];
  }
  if (!factory) {
    result.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    result.diagnostic = "native still-image platform provider is not registered";
    return result;
  }

  auto services = factory(config.selected_backend, config.context_identity,
                          config.device_identity);
  if (!services_complete(services)) {
    result.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    result.diagnostic = "native still-image platform provider is incomplete";
    return result;
  }

  NativeStillImageHostConfig host_config;
  host_config.platform = config.platform;
  host_config.backend = config.selected_backend;
  host_config.context_identity = config.context_identity;
  host_config.device_identity = std::move(config.device_identity);
  host_config.limits = config.limits;
  host_config.progress = std::move(config.progress);
  host_config.services = std::move(services);
  auto host = make_native_still_image_host(std::move(host_config));
  if (!host) {
    result.result = host.result;
    result.diagnostic = std::move(host.diagnostic);
    return result;
  }

  result.runtime_config.gpu_host = std::move(host.host);
  result.runtime_config.allow_cpu_fallback = false;
  result.runtime_config.process_cpu = {};
  result.result = DIGITOR_RESULT_OK;
  return result;
}

}  // namespace digitor
