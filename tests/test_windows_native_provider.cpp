#include "digitor/windows_native_provider.hpp"

#include <cassert>

using namespace digitor;

int main() {
  WindowsNativeProviderInputs empty{};
  assert(!validate_windows_native_provider_inputs(empty));

  static int device;
  WindowsNativeProviderInputs inputs{};
  inputs.package_identity = "digitor-windows-native";
  inputs.build_identity = "test-build";
  inputs.timeline.backend = DIGITOR_RENDERER_D3D12;
  inputs.timeline.context_identity = &device;
  inputs.timeline.device_identity = "timeline-d3d12";
  inputs.timeline.create_target = [](std::uint32_t, std::uint32_t, std::int64_t)
      -> std::optional<ProcessedGpuFramePtr> { return std::nullopt; };
  inputs.timeline.execute_effects = [](const VideoExecutionLayer&,
                                       const ProcessedGpuFramePtr&,
                                       ProcessedGpuFramePtr&,
                                       std::string&) {
    return DIGITOR_RESULT_OK;
  };
  inputs.timeline.composite_layer = [](const VideoExecutionLayer&,
                                       const ProcessedGpuFramePtr&,
                                       const ProcessedGpuFramePtr&,
                                       ProcessedGpuFramePtr&,
                                       std::string&) {
    return DIGITOR_RESULT_OK;
  };
  inputs.timeline.frame_evictable = [](const ProcessedGpuFrame&) { return true; };

  inputs.flutter_texture.implementation_identity = "flutter-external-texture";
  inputs.flutter_texture.d3d_device_identity = &device;
  inputs.flutter_texture.attached = [] { return true; };
  inputs.flutter_texture.register_or_present =
      [](const ProcessedGpuFramePtr&, std::uint64_t) {
        return DIGITOR_RESULT_OK;
      };
  inputs.flutter_texture.accepts_d3d12_resource = true;
  inputs.flutter_texture.retains_until_consumed = true;
  inputs.flutter_texture.native_fence_wait_bound = true;
  inputs.flutter_texture.zero_cpu_readback = true;
  inputs.flutter_texture.zero_cpu_staging = true;

  inputs.encoder.implementation_identity = "mf-nvenc-qsv";
  inputs.encoder.media_foundation_bound = true;
  inputs.encoder.nvenc_or_qsv_bound = true;
  inputs.encoder.d3d12_resource_input = true;
  inputs.encoder.native_fence_wait_bound = true;
  inputs.encoder.zero_cpu_readback = true;
  inputs.encoder.zero_cpu_staging = true;
  inputs.encoder.host.open = [](const HardwareEncodeConfig&,
                                const ExportRenderSnapshot&,
                                WindowsHardwareEncodeCapabilities&,
                                std::string&) { return DIGITOR_RESULT_OK; };
  inputs.encoder.host.submit = [](const WindowsHardwareEncodeFrameDescriptor&,
                                  std::string&) { return DIGITOR_RESULT_OK; };
  inputs.encoder.host.drain = [](std::string&) { return DIGITOR_RESULT_OK; };
  inputs.encoder.host.finalize_atomic = [](std::string&) {
    return DIGITOR_RESULT_OK;
  };
  inputs.encoder.host.cancel = [] {};
  inputs.encoder.host.qualification = [] {
    return WindowsHardwareEncodeQualification{};
  };

  const auto validation = validate_windows_native_provider_inputs(inputs);
  assert(validation);
  const auto provider = create_windows_native_platform_provider(inputs);
  assert(validate_native_platform_provider(provider));

  auto vulkan = inputs;
  vulkan.timeline.backend = DIGITOR_RENDERER_VULKAN;
  assert(!validate_windows_native_provider_inputs(vulkan));
  return 0;
}
