#pragma once

#include "digitor/windows_hardware_encode_adapter.hpp"
#include "export_m4_android_contract_cases.hpp"

#include <atomic>
#include <cassert>
#include <memory>
#include <string>

namespace {

inline digitor::ProcessedGpuFramePtr m3_frame(std::int64_t pts) {
  static int context;
  digitor::GpuFrameMetadata metadata{};
  metadata.width = 1920;
  metadata.height = 1080;
  metadata.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  metadata.timestamp = pts;
  metadata.color_metadata = "linear-rgba";
  return std::make_shared<digitor::ProcessedGpuFrame>(
      &context, DIGITOR_RENDERER_D3D12, metadata, static_cast<std::uint64_t>(pts + 1),
      std::static_pointer_cast<void>(std::make_shared<int>(1)),
      std::make_shared<std::atomic_bool>(true), false);
}

inline std::shared_ptr<const digitor::ExportRenderSnapshot> m3_snapshot() {
  digitor::ExportRenderSnapshotData d{};
  d.snapshot_identity = 3001;
  d.timeline_revision = 1;
  d.render_revision = 2;
  d.node_graph_revision = 3;
  d.color_pipeline_revision = 4;
  d.audio_revision = 5;
  d.width = 1920;
  d.height = 1080;
  d.working_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  d.fps_num = 30;
  d.fps_den = 1;
  d.duration_us = 100000;
  d.color_metadata = "linear-rgba";
  d.output_path = "m3-output.mp4";
  d.profile.width = 1920;
  d.profile.height = 1080;
  d.profile.fps_num = 30;
  d.profile.fps_den = 1;
  d.profile.codec = digitor::ExportCodec::h264;
  d.profile.prefer_hardware = true;
  d.profile.allow_software_fallback = false;
  d.policy = digitor::ExportExecutionPolicy::hardware_required;
  d.renderer_backend = DIGITOR_RENDERER_D3D12;
  d.encoder_backend = digitor::EncoderBackend::nvenc;
  return std::make_shared<const digitor::ExportRenderSnapshot>(std::move(d));
}

inline void run_export_m3_windows_contract_cases() {
  using namespace digitor;
  auto snapshot = m3_snapshot();
  WindowsHardwareEncodeQualification q{};
  std::uint64_t cancelled = 0;

  WindowsHardwareEncoderHost host{};
  host.open = [&](const HardwareEncodeConfig& config,
                  const ExportRenderSnapshot& frozen,
                  WindowsHardwareEncodeCapabilities& caps,
                  std::string&) {
    assert(config.require_hardware && config.require_zero_copy);
    assert(config.output_path == frozen.data().output_path);
    caps.api = WindowsHardwareEncoderApi::nvenc;
    caps.interop = WindowsGpuInterop::d3d12_native;
    caps.available = true;
    caps.h264 = true;
    caps.hevc = true;
    caps.av1 = true;
    caps.ten_bit = true;
    caps.hdr_metadata = true;
    caps.mp4 = true;
    caps.mov = true;
    caps.mkv = true;
    caps.max_width = 8192;
    caps.max_height = 8192;
    caps.device_identity = "test-d3d12-device";
    q.adapter_opened = true;
    return DIGITOR_RESULT_OK;
  };
  host.submit = [&](const WindowsHardwareEncodeFrameDescriptor& frame,
                    std::string&) {
    assert(frame.frame && frame.frame->backend() == DIGITOR_RENDERER_D3D12);
    assert(frame.color_metadata == "linear-rgba");
    q.gpu_frame_submitted = true;
    q.synchronization_waited = true;
    q.native_resource_registered = true;
    ++q.submitted_frames;
    ++q.encoded_frames;
    return DIGITOR_RESULT_OK;
  };
  host.drain = [&](std::string&) { q.bitstream_produced = true; return DIGITOR_RESULT_OK; };
  host.finalize_atomic = [&](std::string&) {
    q.atomic_output_finalized = true;
    return DIGITOR_RESULT_OK;
  };
  host.cancel = [&] { ++cancelled; };
  host.qualification = [&] { return q; };

  auto adapter = create_windows_hardware_encode_adapter(snapshot, host);
  HardwareEncodeConfig config{};
  config.profile = snapshot->data().profile;
  config.backend = EncoderBackend::nvenc;
  config.output_path = snapshot->data().output_path;
  config.duration_us = snapshot->data().duration_us;
  config.require_hardware = true;
  config.require_zero_copy = true;
  config.require_atomic_finalize = true;

  ProductionHardwareEncodeSession session(config, adapter.callbacks);
  std::string diagnostic;
  assert(session.start(&diagnostic) == DIGITOR_RESULT_OK);
  assert(session.submit({m3_frame(0), 0, 33333, true}, &diagnostic) == DIGITOR_RESULT_OK);
  assert(session.submit({m3_frame(33333), 33333, 33333, false}, &diagnostic) == DIGITOR_RESULT_OK);
  assert(session.finish(&diagnostic) == DIGITOR_RESULT_OK);
  const auto qualified = adapter.qualification();
  assert(qualified.adapter_opened && qualified.native_resource_registered);
  assert(qualified.synchronization_waited && qualified.bitstream_produced);
  assert(qualified.atomic_output_finalized && qualified.no_cpu_readback);
  assert(qualified.no_cpu_staging && qualified.encoded_frames == 2);

  auto unsupported = snapshot->data();
  unsupported.output_path = "bad.avi";
  const ExportRenderSnapshot unsupported_snapshot(std::move(unsupported));
  WindowsHardwareEncodeCapabilities caps{};
  caps.api = WindowsHardwareEncoderApi::nvenc;
  caps.interop = WindowsGpuInterop::d3d12_native;
  caps.available = true;
  caps.h264 = true;
  caps.mp4 = true;
  caps.max_width = 8192;
  caps.max_height = 8192;
  caps.device_identity = "device";
  assert(!validate_windows_hardware_encode_contract(unsupported_snapshot, caps));

  WindowsHardwareEncoderHost violating_host = host;
  WindowsHardwareEncodeQualification bad_q = q;
  bad_q.no_cpu_staging = false;
  violating_host.qualification = [&] { return bad_q; };
  auto violating_adapter = create_windows_hardware_encode_adapter(snapshot, violating_host);
  ProductionHardwareEncodeSession violating(config, violating_adapter.callbacks);
  assert(violating.start() == DIGITOR_RESULT_OK);
  assert(violating.submit({m3_frame(0), 0, 33333, true}) != DIGITOR_RESULT_OK);
  assert(cancelled >= 1);

  run_export_m4_android_contract_cases();
}

}  // namespace
