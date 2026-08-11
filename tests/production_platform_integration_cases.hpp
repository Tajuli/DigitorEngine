#pragma once

#include "digitor/production_platform_integration.hpp"
#include "source_release_readiness_cases.hpp"

#include <atomic>
#include <cassert>
#include <memory>

inline void run_production_platform_integration_cases() {
  using namespace digitor;

  static int context;
  ExportRenderSnapshotData data{};
  data.snapshot_identity = 7001;
  data.timeline_revision = 1;
  data.render_revision = 2;
  data.node_graph_revision = 3;
  data.color_pipeline_revision = 4;
  data.audio_revision = 5;
  data.width = 1920;
  data.height = 1080;
  data.working_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  data.fps_num = 30;
  data.fps_den = 1;
  data.duration_us = 33333;
  data.color_metadata = "linear-rgba";
  data.output_path = "platform-output.mp4";
  data.profile.width = 1920;
  data.profile.height = 1080;
  data.profile.fps_num = 30;
  data.profile.fps_den = 1;
  data.profile.codec = ExportCodec::h264;
  data.profile.prefer_hardware = true;
  data.profile.allow_software_fallback = false;
  data.policy = ExportExecutionPolicy::hardware_required;
  data.renderer_backend = DIGITOR_RENDERER_D3D12;
  data.encoder_backend = EncoderBackend::nvenc;
  auto snapshot = std::make_shared<const ExportRenderSnapshot>(std::move(data));

  auto make_frame = [&](std::int64_t timestamp, std::uint64_t identity) {
    GpuFrameMetadata metadata{};
    metadata.width = 1920;
    metadata.height = 1080;
    metadata.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
    metadata.timestamp = timestamp;
    metadata.color_metadata = "linear-rgba";
    return std::make_shared<ProcessedGpuFrame>(
        &context, DIGITOR_RENDERER_D3D12, metadata, identity,
        std::static_pointer_cast<void>(std::make_shared<int>(1)),
        std::make_shared<std::atomic_bool>(true), false);
  };

  ProductionPlatformFactoryInputs inputs{};
  inputs.platform = ProductionPlatform::windows;
  inputs.timeline.backend = DIGITOR_RENDERER_D3D12;
  inputs.timeline.context_identity = &context;
  inputs.timeline.device_identity = "windows-test-device";
  inputs.timeline.create_target = [&](std::uint32_t, std::uint32_t,
                                      std::int64_t timestamp)
      -> std::optional<ProcessedGpuFramePtr> {
    return make_frame(timestamp, 10);
  };
  inputs.timeline.execute_effects = [&](const VideoExecutionLayer&,
                                        const ProcessedGpuFramePtr& input,
                                        ProcessedGpuFramePtr& output,
                                        std::string&) {
    output = make_frame(input->metadata().timestamp, 11);
    return DIGITOR_RESULT_OK;
  };
  inputs.timeline.composite_layer = [&](const VideoExecutionLayer&,
                                        const ProcessedGpuFramePtr&,
                                        const ProcessedGpuFramePtr& target,
                                        ProcessedGpuFramePtr& output,
                                        std::string&) {
    output = make_frame(target->metadata().timestamp, 12);
    return DIGITOR_RESULT_OK;
  };
  inputs.timeline.frame_evictable = [](const ProcessedGpuFrame& frame) {
    return frame.ready();
  };

  bool flutter_presented = false;
  inputs.flutter.platform = ProductionPlatform::windows;
  inputs.flutter.backend = DIGITOR_RENDERER_D3D12;
  inputs.flutter.device_identity = &context;
  inputs.flutter.device_name = "windows-test-device";
  inputs.flutter.attached = [] { return true; };
  inputs.flutter.register_or_present = [&](const ProcessedGpuFramePtr& frame,
                                           std::uint64_t generation) {
    flutter_presented = frame && generation == 1;
    return flutter_presented ? DIGITOR_RESULT_OK : DIGITOR_RESULT_INVALID_ARGUMENT;
  };

  WindowsHardwareEncodeQualification qualification{};
  inputs.encoder.windows.open = [&](const HardwareEncodeConfig&,
                                    const ExportRenderSnapshot&,
                                    WindowsHardwareEncodeCapabilities& caps,
                                    std::string&) {
    caps.api = WindowsHardwareEncoderApi::nvenc;
    caps.interop = WindowsGpuInterop::d3d12_native;
    caps.available = true;
    caps.h264 = true;
    caps.ten_bit = true;
    caps.mp4 = true;
    caps.max_width = 8192;
    caps.max_height = 8192;
    caps.device_identity = "windows-test-device";
    qualification.adapter_opened = true;
    return DIGITOR_RESULT_OK;
  };
  inputs.encoder.windows.submit = [&](const WindowsHardwareEncodeFrameDescriptor&,
                                      std::string&) {
    qualification.gpu_frame_submitted = true;
    qualification.synchronization_waited = true;
    qualification.native_resource_registered = true;
    return DIGITOR_RESULT_OK;
  };
  inputs.encoder.windows.drain = [](std::string&) {
    return DIGITOR_RESULT_OK;
  };
  inputs.encoder.windows.finalize_atomic = [&](std::string&) {
    qualification.bitstream_produced = true;
    qualification.atomic_output_finalized = true;
    return DIGITOR_RESULT_OK;
  };
  inputs.encoder.windows.cancel = [] {};
  inputs.encoder.windows.qualification = [&] { return qualification; };

  auto assembly = create_production_platform_assembly(std::move(inputs));
  assert(assembly);
  // #344 regression: constructing the production assembly and presenting
  // preview frames must not open or instantiate the hardware encoder.
  assert(!qualification.adapter_opened);
  assert(assembly.preview_host->present(make_frame(0, 20), 1) == DIGITOR_RESULT_OK);
  assert(flutter_presented);
  assert(!qualification.adapter_opened);
  // Preview assembly must be valid before any export snapshot exists.
  assert(!hardware_encoder_callbacks_complete(assembly.encoder_callbacks));
  assert(assembly.encoder_factory);
  auto lazy_encoder = assembly.encoder_factory(snapshot);
  assert(lazy_encoder);
  // The snapshot-bound lazy factory may prepare callbacks, but native encoder
  // ownership is still deferred until the export session starts.
  assert(!qualification.adapter_opened);

  HardwareEncodeConfig config{};
  config.backend = EncoderBackend::nvenc;
  config.profile = snapshot->data().profile;
  config.output_path = snapshot->data().output_path;
  config.duration_us = snapshot->data().duration_us;
  config.require_hardware = true;
  config.require_zero_copy = true;
  config.require_atomic_finalize = true;
  ProductionHardwareEncodeSession session(config, std::move(lazy_encoder.callbacks));
  assert(session.start() == DIGITOR_RESULT_OK);
  assert(qualification.adapter_opened);
  assert(session.submit({make_frame(0, 21), 0, 33333, true}) == DIGITOR_RESULT_OK);
  assert(session.finish() == DIGITOR_RESULT_OK);
  assert(lazy_encoder.zero_copy_qualified());

  WindowsVulkanZeroCopyInterop invalid{};
  assert(!validate_windows_vulkan_zero_copy_interop(invalid));
  invalid.available = true;
  invalid.dxgi_external_memory = true;
  invalid.external_semaphore = true;
  invalid.nv12 = true;
  invalid.p010 = true;
  invalid.adapter_identity_matched = true;
  invalid.adapter_identity = "same-adapter";
  invalid.export_to_d3d12_encoder_resource =
      [](const ProcessedGpuFramePtr&, ProcessedGpuFramePtr&, std::string&) {
        return DIGITOR_RESULT_OK;
      };
  assert(validate_windows_vulkan_zero_copy_interop(invalid));

  run_source_release_readiness_cases();
}
