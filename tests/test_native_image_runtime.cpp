#include "digitor/native_image_runtime.hpp"

#include <cassert>
#include <string>

int main() {
  using namespace digitor;

  clear_native_image_platform_services();
  assert(!native_image_platform_services_available());

  NativeImageRuntimePlatformServices invalid;
  invalid.platform = NativeStillPlatform::windows;
  invalid.backend = DIGITOR_RENDERER_METAL;
  assert(register_native_image_platform_services(std::move(invalid)) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);

  NativeImageRuntimePlatformServices services;
  services.platform = NativeStillPlatform::windows;
  services.backend = DIGITOR_RENDERER_D3D12;
  services.context_identity = reinterpret_cast<const void*>(0x1);
  services.device_identity = "fixture-d3d12";
  services.image_services.decode_to_gpu =
      [](const std::string&, NativeStillImageInfo&, ProcessedGpuFramePtr&,
         std::string& diagnostic) {
        diagnostic = "fixture decode failure";
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      };
  services.image_services.resize_gpu =
      [](const ProcessedGpuFramePtr&, std::uint32_t, std::uint32_t,
         std::int64_t, ProcessedGpuFramePtr&, std::string&) {
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      };
  services.image_services.process_graph =
      [](const GpuImageSessionProcessRequest&, ProcessedGpuFramePtr&,
         std::string&) { return DIGITOR_RESULT_BACKEND_UNAVAILABLE; };
  services.image_services.encode_from_gpu =
      [](const ProcessedGpuFramePtr&, const std::string&,
         const ImageExportOptions&, const NativeStillImageInfo&,
         const NativeStillImageProgress&) {
        return ImageIoResult{DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                             "fixture encode failure"};
      };

  assert(register_native_image_platform_services(std::move(services)) ==
         DIGITOR_RESULT_OK);
  assert(native_image_platform_services_available());

  NativeImageRuntimeOpenRequest request;
  request.path = "fixture.png";
  request.options.allow_cpu_fallback = true;
  request.process_cpu =
      [](const ImageEditorCpuProcessRequest&, RenderVideoFrame&,
         std::string&) { return DIGITOR_RESULT_OK; };
  auto result = open_native_image_runtime(std::move(request));
  assert(!result);
  assert(result.selected_backend == ImageEditorExecutionBackend::gpu);
  assert(result.result.result == DIGITOR_RESULT_BACKEND_UNAVAILABLE);

  clear_native_image_platform_services();
  assert(!native_image_platform_services_available());
  return 0;
}
