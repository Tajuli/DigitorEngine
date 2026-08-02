#pragma once

#include "digitor/android_hardware_encode_adapter.hpp"

#include <atomic>
#include <cassert>
#include <memory>

inline void run_export_m4_android_contract_cases() {
  using namespace digitor;

  ExportRenderSnapshotData data{};
  data.snapshot_identity = 4001;
  data.timeline_revision = 41;
  data.render_revision = 42;
  data.node_graph_revision = 43;
  data.color_pipeline_revision = 44;
  data.audio_revision = 45;
  data.width = 1920;
  data.height = 1080;
  data.working_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  data.fps_num = 30;
  data.fps_den = 1;
  data.duration_us = 33333;
  data.color_metadata = "linear-rgba";
  data.output_path = "android-output.mp4";
  data.profile.width = 1920;
  data.profile.height = 1080;
  data.profile.fps_num = 30;
  data.profile.fps_den = 1;
  data.profile.codec = ExportCodec::h264;
  data.profile.prefer_hardware = true;
  data.profile.allow_software_fallback = false;
  data.policy = ExportExecutionPolicy::hardware_required;
  data.renderer_backend = DIGITOR_RENDERER_VULKAN;
  data.encoder_backend = EncoderBackend::media_codec;
  auto snapshot = std::make_shared<const ExportRenderSnapshot>(std::move(data));

  AndroidHardwareEncodeQualification q{};
  AndroidHardwareEncoderHost host{};
  host.open = [&](const HardwareEncodeConfig&, const ExportRenderSnapshot&,
                  AndroidHardwareEncodeCapabilities& caps, std::string&) {
    caps.available = true;
    caps.hardware_codec = true;
    caps.input_surface = true;
    caps.interop = AndroidGpuInterop::vulkan_ahardwarebuffer;
    caps.h264 = true;
    caps.hevc = true;
    caps.ten_bit = true;
    caps.hdr_metadata = true;
    caps.mp4 = true;
    caps.max_width = 3840;
    caps.max_height = 2160;
    caps.api_level = 30;
    caps.codec_name = "c2.vendor.avc.encoder";
    caps.device_identity = "android-vulkan-device";
    q.codec_opened = true;
    q.input_surface_created = true;
    return DIGITOR_RESULT_OK;
  };
  host.submit = [&](const AndroidHardwareEncodeFrameDescriptor& frame,
                    std::string&) {
    assert(frame.frame && frame.frame->backend() == DIGITOR_RENDERER_VULKAN);
    q.gpu_frame_submitted = true;
    q.acquire_sync_waited = true;
    q.release_sync_published = true;
    q.ahardwarebuffer_or_surface_bound = true;
    ++q.submitted_frames;
    ++q.encoded_frames;
    return DIGITOR_RESULT_OK;
  };
  host.drain = [](std::string&) { return DIGITOR_RESULT_OK; };
  host.finalize_mp4_atomic = [&](std::string&) {
    q.bitstream_produced = true;
    q.mp4_finalized = true;
    return DIGITOR_RESULT_OK;
  };
  host.cancel = [] {};
  host.qualification = [&] { return q; };

  auto adapter = create_android_hardware_encode_adapter(snapshot, host);
  HardwareEncodeConfig config{};
  config.profile = snapshot->data().profile;
  config.backend = EncoderBackend::media_codec;
  config.output_path = snapshot->data().output_path;
  config.duration_us = snapshot->data().duration_us;
  std::string diagnostic;
  assert(adapter.callbacks.open(config, diagnostic) == DIGITOR_RESULT_OK);

  static int context;
  GpuFrameMetadata metadata{};
  metadata.width = 1920;
  metadata.height = 1080;
  metadata.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  metadata.timestamp = 0;
  metadata.color_metadata = "linear-rgba";
  auto frame = std::make_shared<ProcessedGpuFrame>(
      &context, DIGITOR_RENDERER_VULKAN, metadata, 77,
      std::static_pointer_cast<void>(std::make_shared<int>(1)),
      std::make_shared<std::atomic_bool>(true), false);
  assert(adapter.callbacks.submit_gpu_frame({frame, 0, 33333, true}, diagnostic) ==
         DIGITOR_RESULT_OK);
  assert(adapter.callbacks.drain(diagnostic) == DIGITOR_RESULT_OK);
  assert(adapter.callbacks.finalize_atomic(diagnostic) == DIGITOR_RESULT_OK);
  const auto qualified = adapter.qualification();
  assert(qualified.no_cpu_readback && qualified.no_cpu_staging);
  assert(qualified.bitstream_produced && qualified.mp4_finalized);

  auto invalid_data = snapshot->data();
  invalid_data.renderer_backend = DIGITOR_RENDERER_D3D12;
  const ExportRenderSnapshot invalid_snapshot(std::move(invalid_data));
  AndroidHardwareEncodeCapabilities caps{};
  caps.available = true;
  caps.hardware_codec = true;
  caps.input_surface = true;
  assert(!validate_android_hardware_encode_contract(invalid_snapshot, caps));
}
