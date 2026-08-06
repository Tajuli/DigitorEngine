#pragma once

#include "digitor/image_editor_runtime.hpp"
#include "digitor/native_still_image_host.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace digitor {

// Platform applications register one concrete service provider during startup:
// WIC on Windows, ImageDecoder/NDK codecs on Android, and ImageIO on Apple.
// Registration is process-wide and intentionally happens before image sessions
// are created, so a selected GPU session never silently falls back to CPU.
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
};

struct ProductionNativeImageRuntimeResult {
  DigitorResult result{DIGITOR_RESULT_OK};
  std::string diagnostic;
  ImageEditorRuntimeHost host;

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

// Creates one locked image-editor host. GPU backends require a registered,
// complete native provider. CPU is selected only when the engine selected CPU
// before session creation; GPU provider failure is returned as an error.
[[nodiscard]] ProductionNativeImageRuntimeResult
make_production_native_image_runtime(ProductionNativeImageRuntimeConfig config);

}  // namespace digitor
