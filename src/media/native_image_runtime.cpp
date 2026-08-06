#include "digitor/native_image_runtime.hpp"

#include <mutex>
#include <optional>
#include <utility>

namespace digitor {
namespace {
std::mutex g_services_mutex;
std::optional<NativeImageRuntimePlatformServices> g_services;

bool platform_services_valid(
    const NativeImageRuntimePlatformServices& services) noexcept {
  return services.complete() && native_still_backend_matches_platform(
                                    services.platform, services.backend);
}
}  // namespace

DigitorResult register_native_image_platform_services(
    NativeImageRuntimePlatformServices services) noexcept {
  if (!platform_services_valid(services))
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  try {
    std::lock_guard lock(g_services_mutex);
    if (g_services) return DIGITOR_RESULT_ALREADY_INITIALIZED;
    g_services = std::move(services);
    return DIGITOR_RESULT_OK;
  } catch (...) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  }
}

void clear_native_image_platform_services() noexcept {
  std::lock_guard lock(g_services_mutex);
  g_services.reset();
}

bool native_image_platform_services_available() noexcept {
  std::lock_guard lock(g_services_mutex);
  return g_services.has_value() && platform_services_valid(*g_services);
}

NativeImageRuntimeOpenResult open_native_image_runtime(
    NativeImageRuntimeOpenRequest request) {
  NativeImageRuntimeOpenResult output;
  if (request.path.empty()) {
    output.result = {DIGITOR_RESULT_INVALID_ARGUMENT,
                     "native image path is empty"};
    return output;
  }

  std::optional<NativeImageRuntimePlatformServices> services;
  {
    std::lock_guard lock(g_services_mutex);
    services = g_services;
  }

  ImageEditorRuntimeConfig runtime_config;
  runtime_config.allow_cpu_fallback = request.options.allow_cpu_fallback;
  runtime_config.process_cpu = std::move(request.process_cpu);

  if (services && platform_services_valid(*services)) {
    NativeStillImageHostConfig host_config;
    host_config.platform = services->platform;
    host_config.backend = services->backend;
    host_config.context_identity = services->context_identity;
    host_config.device_identity = services->device_identity;
    host_config.limits = request.options.limits;
    host_config.progress = request.options.progress;
    host_config.services = std::move(services->image_services);
    auto host = make_native_still_image_host(std::move(host_config));
    if (!host) {
      output.result = {host.result, std::move(host.diagnostic)};
      return output;
    }
    runtime_config.gpu_host = std::move(host.host);
    output.selected_backend = ImageEditorExecutionBackend::gpu;
  } else {
    output.selected_backend = ImageEditorExecutionBackend::cpu;
  }

  auto [runtime, result] =
      ImageEditorRuntime::open(std::move(request.path),
                               std::move(runtime_config));
  output.runtime = std::move(runtime);
  output.result = std::move(result);
  return output;
}

}  // namespace digitor
