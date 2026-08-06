#pragma once

#include "digitor/image_editor_runtime.hpp"
#include "digitor/native_still_image_host.hpp"

#include <functional>
#include <memory>
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
    return context_identity != nullptr && !device_identity.empty() &&
           image_services.decode_to_gpu && image_services.resize_gpu &&
           image_services.process_graph && image_services.encode_from_gpu;
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

// One process-wide platform service registration is used by the app/plugin
// integration. Registration is rejected while another provider is installed.
// The provider owns the WIC/ImageDecoder/ImageIO implementation and the native
// D3D12/Vulkan/GLES/Metal context used by decode, upload, resize and export.
DigitorResult register_native_image_platform_services(
    NativeImageRuntimePlatformServices services) noexcept;
void clear_native_image_platform_services() noexcept;
[[nodiscard]] bool native_image_platform_services_available() noexcept;

// Opens one locked image editor backend. A complete GPU provider selects GPU;
// failures after that selection are returned and never fall back to CPU. CPU is
// selected only when no complete GPU provider is registered and fallback is
// enabled.
[[nodiscard]] NativeImageRuntimeOpenResult open_native_image_runtime(
    NativeImageRuntimeOpenRequest request);

}  // namespace digitor
