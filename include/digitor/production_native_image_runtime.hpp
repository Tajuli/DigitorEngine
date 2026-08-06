#pragma once

#include "digitor/image_editor_runtime.hpp"
#include "digitor/native_still_image_host.hpp"

#include <functional>
#include <string>

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

void register_native_still_image_services(
    NativeStillPlatform platform,
    NativeStillImageServiceFactory factory);

void clear_native_still_image_services_for_test() noexcept;

[[nodiscard]] bool native_still_image_services_registered(
    NativeStillPlatform platform) noexcept;

// Builds a backend-locked image-editor configuration. GPU provider failure is
// returned to the caller and never triggers a silent CPU processing fallback.
[[nodiscard]] ProductionNativeImageRuntimeResult
make_production_native_image_runtime(ProductionNativeImageRuntimeConfig config);

}  // namespace digitor
