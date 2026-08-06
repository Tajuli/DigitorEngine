#include "digitor/native_still_image_host.hpp"
#include "digitor/production_native_image_runtime.hpp"

#include <cassert>
#include <string>

int main() {
  using namespace digitor;

  assert(native_still_backend_matches_platform(
      NativeStillPlatform::windows, DIGITOR_RENDERER_D3D12));
  assert(native_still_backend_matches_platform(
      NativeStillPlatform::windows, DIGITOR_RENDERER_VULKAN));
  assert(!native_still_backend_matches_platform(
      NativeStillPlatform::windows, DIGITOR_RENDERER_METAL));
  assert(native_still_backend_matches_platform(
      NativeStillPlatform::android, DIGITOR_RENDERER_VULKAN));
  assert(native_still_backend_matches_platform(
      NativeStillPlatform::android, DIGITOR_RENDERER_OPENGL_ES));
  assert(native_still_backend_matches_platform(
      NativeStillPlatform::apple, DIGITOR_RENDERER_METAL));

  NativeStillImageHostConfig invalid;
  invalid.platform = NativeStillPlatform::windows;
  invalid.backend = DIGITOR_RENDERER_METAL;
  auto result = make_native_still_image_host(invalid);
  assert(!result);
  assert(result.result == DIGITOR_RESULT_INVALID_ARGUMENT);

  NativeStillImageHostConfig config;
  config.platform = NativeStillPlatform::windows;
  config.backend = DIGITOR_RENDERER_D3D12;
  config.context_identity = reinterpret_cast<const void*>(0x1);
  config.device_identity = "test-d3d12-device";
  config.services.decode_to_gpu =
      [](const std::string&, NativeStillImageInfo&, ProcessedGpuFramePtr&,
         std::string&) { return DIGITOR_RESULT_BACKEND_UNAVAILABLE; };
  config.services.resize_gpu =
      [](const ProcessedGpuFramePtr&, std::uint32_t, std::uint32_t,
         std::int64_t, ProcessedGpuFramePtr&, std::string&) {
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      };
  config.services.process_graph =
      [](const GpuImageSessionProcessRequest&, ProcessedGpuFramePtr&,
         std::string&) { return DIGITOR_RESULT_BACKEND_UNAVAILABLE; };
  config.services.encode_from_gpu =
      [](const ProcessedGpuFramePtr&, const std::string&,
         const ImageExportOptions&, const NativeStillImageInfo&,
         const NativeStillImageProgress&) {
        return ImageIoResult{DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                             "test encoder unavailable"};
      };

  result = make_native_still_image_host(std::move(config));
  assert(result);
  assert(result.host.image_io.backend == DIGITOR_RENDERER_D3D12);
  assert(result.host.image_io.context_identity != nullptr);
  assert(gpu_image_session_host_valid(result.host));

  clear_native_still_image_services_for_test();
  ProductionNativeImageRuntimeConfig production;
  production.platform = NativeStillPlatform::windows;
  production.selected_backend = DIGITOR_RENDERER_CPU;
  production.process_cpu =
      [](const ImageEditorCpuProcessRequest& request, RenderVideoFrame& output,
         std::string&) {
        output = request.source;
        return DIGITOR_RESULT_OK;
      };
  auto locked = make_production_native_image_runtime(std::move(production));
  assert(locked);
  assert(locked.runtime_config.allow_cpu_fallback);
  assert(locked.runtime_config.process_cpu);
  assert(!gpu_image_session_host_valid(locked.runtime_config.gpu_host));

  ProductionNativeImageRuntimeConfig missing_gpu;
  missing_gpu.platform = NativeStillPlatform::windows;
  missing_gpu.selected_backend = DIGITOR_RENDERER_D3D12;
  missing_gpu.context_identity = reinterpret_cast<const void*>(0x2);
  missing_gpu.device_identity = "missing-provider";
  locked = make_production_native_image_runtime(std::move(missing_gpu));
  assert(!locked);
  assert(locked.result == DIGITOR_RESULT_BACKEND_UNAVAILABLE);

  register_native_still_image_services(
      NativeStillPlatform::windows,
      [](DigitorRendererBackend, const void*, const std::string&) {
        NativeStillImageServices services;
        services.decode_to_gpu =
            [](const std::string&, NativeStillImageInfo&,
               ProcessedGpuFramePtr&, std::string&) {
              return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            };
        services.resize_gpu =
            [](const ProcessedGpuFramePtr&, std::uint32_t, std::uint32_t,
               std::int64_t, ProcessedGpuFramePtr&, std::string&) {
              return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            };
        services.process_graph =
            [](const GpuImageSessionProcessRequest&, ProcessedGpuFramePtr&,
               std::string&) { return DIGITOR_RESULT_BACKEND_UNAVAILABLE; };
        services.encode_from_gpu =
            [](const ProcessedGpuFramePtr&, const std::string&,
               const ImageExportOptions&, const NativeStillImageInfo&,
               const NativeStillImageProgress&) {
              return ImageIoResult{DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                                   "fixture encoder unavailable"};
            };
        return services;
      });
  assert(native_still_image_services_registered(
      NativeStillPlatform::windows));

  ProductionNativeImageRuntimeConfig gpu;
  gpu.platform = NativeStillPlatform::windows;
  gpu.selected_backend = DIGITOR_RENDERER_D3D12;
  gpu.context_identity = reinterpret_cast<const void*>(0x3);
  gpu.device_identity = "registered-provider";
  locked = make_production_native_image_runtime(std::move(gpu));
  assert(locked);
  assert(!locked.runtime_config.allow_cpu_fallback);
  assert(gpu_image_session_host_valid(locked.runtime_config.gpu_host));

  clear_native_still_image_services_for_test();
  return 0;
}
