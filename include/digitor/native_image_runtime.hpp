#pragma once

#include "digitor/image_editor_runtime.hpp"
#include "digitor/native_still_image_host.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace digitor {

struct NativeImageRuntimePlatformServices {
  NativeStillPlatform platform{NativeStillPlatform::windows};
  DigitorRendererBackend backend{DIGITOR_RENDERER_AUTO};
  const void* context_identity{};
  std::string device_identity;
  NativeStillImageServices image_services;

  [[nodiscard]] bool complete() const noexcept {
    return backend != DIGITOR_RENDERER_AUTO && context_identity != nullptr &&
           !device_identity.empty() && image_services.decode_to_gpu &&
           image_services.resize_gpu && image_services.process_graph &&
           image_services.encode_from_gpu &&
           native_still_backend_matches_platform(platform, backend);
  }
};

struct NativeImageRuntimeOptions {
  NativeStillImageLimits limits{};
  NativeStillImageProgress progress{};
  bool allow_cpu_fallback{true};
};

struct NativeImageRuntimeOpenRequest {
  std::string path;
  NativeImageRuntimeOptions options{};
  std::function<DigitorResult(const ImageEditorCpuProcessRequest&,
                              RenderVideoFrame&, std::string&)>
      process_cpu;
};

struct NativeImageRuntimeOpenResult {
  std::unique_ptr<ImageEditorRuntime> runtime;
  ImageIoResult result;
  ImageEditorExecutionBackend selected_backend{
      ImageEditorExecutionBackend::cpu};

  [[nodiscard]] explicit operator bool() const noexcept {
    return runtime != nullptr && static_cast<bool>(result);
  }
};

namespace native_image_runtime_detail {
inline std::mutex services_mutex;
inline std::optional<NativeImageRuntimePlatformServices> services;
}  // namespace native_image_runtime_detail

// Registers the concrete WIC/ImageDecoder/ImageIO provider owned by the
// platform plugin. Registration is process-wide and immutable until cleared.
// This prevents a live session from changing native contexts underneath GPU
// textures. A provider is accepted only when every required callback is present
// and its renderer backend matches the platform contract.
inline DigitorResult register_native_image_platform_services(
    NativeImageRuntimePlatformServices value) noexcept {
  if (!value.complete()) return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::lock_guard lock(native_image_runtime_detail::services_mutex);
  if (native_image_runtime_detail::services)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  native_image_runtime_detail::services.emplace(std::move(value));
  return DIGITOR_RESULT_OK;
}

inline void clear_native_image_platform_services() noexcept {
  std::lock_guard lock(native_image_runtime_detail::services_mutex);
  native_image_runtime_detail::services.reset();
}

[[nodiscard]] inline bool
native_image_platform_services_available() noexcept {
  std::lock_guard lock(native_image_runtime_detail::services_mutex);
  return native_image_runtime_detail::services.has_value();
}

// Opens exactly one locked backend. A complete GPU provider always selects GPU.
// Decode/upload/resize/process/export failures after that point are returned to
// the caller and never trigger CPU processing. CPU is selected only when no
// complete native GPU provider is registered and fallback is explicitly enabled.
[[nodiscard]] inline NativeImageRuntimeOpenResult open_native_image_runtime(
    NativeImageRuntimeOpenRequest request) {
  if (request.path.empty()) {
    return {nullptr,
            ImageIoResult{DIGITOR_RESULT_INVALID_ARGUMENT,
                          "image path is empty"},
            ImageEditorExecutionBackend::cpu};
  }

  std::optional<NativeImageRuntimePlatformServices> provider;
  {
    std::lock_guard lock(native_image_runtime_detail::services_mutex);
    provider = native_image_runtime_detail::services;
  }

  ImageEditorRuntimeConfig config;
  config.allow_cpu_fallback = request.options.allow_cpu_fallback;
  config.process_cpu = std::move(request.process_cpu);

  ImageEditorExecutionBackend selected = ImageEditorExecutionBackend::cpu;
  if (provider) {
    NativeStillImageHostConfig host_config;
    host_config.platform = provider->platform;
    host_config.backend = provider->backend;
    host_config.context_identity = provider->context_identity;
    host_config.device_identity = provider->device_identity;
    host_config.limits = request.options.limits;
    host_config.progress = request.options.progress;
    host_config.services = provider->image_services;

    auto host = make_native_still_image_host(std::move(host_config));
    if (!host) {
      return {nullptr,
              ImageIoResult{host.result,
                            host.diagnostic.empty()
                                ? "native GPU image host creation failed"
                                : std::move(host.diagnostic)},
              ImageEditorExecutionBackend::gpu};
    }
    config.gpu_host = std::move(host.host);
    // A valid host locks the session to GPU inside ImageEditorRuntime::open.
    // CPU fallback remains configured only for the no-provider branch.
    config.allow_cpu_fallback = false;
    selected = ImageEditorExecutionBackend::gpu;
  }

  auto [runtime, result] =
      ImageEditorRuntime::open(std::move(request.path), std::move(config));
  return {std::move(runtime), std::move(result), selected};
}

}  // namespace digitor
