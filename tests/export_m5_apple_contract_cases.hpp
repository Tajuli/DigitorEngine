#pragma once

#include "digitor/apple_hardware_encode_adapter.hpp"

#include <atomic>
#include <cassert>
#include <memory>

namespace {

inline digitor::ProcessedGpuFramePtr m5_frame(std::int64_t pts) {
  static int context;
  digitor::GpuFrameMetadata m{};
  m.width = 1920; m.height = 1080;
  m.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  m.timestamp = pts; m.color_metadata = "linear-rgba";
  return std::make_shared<digitor::ProcessedGpuFrame>(
      &context, DIGITOR_RENDERER_METAL, m, static_cast<std::uint64_t>(pts + 1),
      std::static_pointer_cast<void>(std::make_shared<int>(1)),
      std::make_shared<std::atomic_bool>(true), false);
}

inline std::shared_ptr<const digitor::ExportRenderSnapshot> m5_snapshot() {
  digitor::ExportRenderSnapshotData d{};
  d.snapshot_identity = 5001; d.timeline_revision = 1; d.render_revision = 2;
  d.node_graph_revision = 3; d.color_pipeline_revision = 4; d.audio_revision = 5;
  d.width = 1920; d.height = 1080; d.working_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  d.fps_num = 30; d.fps_den = 1; d.duration_us = 100000;
  d.color_metadata = "linear-rgba"; d.output_path = "m5-output.mov";
  d.profile.width = 1920; d.profile.height = 1080;
  d.profile.fps_num = 30; d.profile.fps_den = 1;
  d.profile.codec = digitor::ExportCodec::hevc; d.profile.ten_bit = true;
  d.profile.prefer_hardware = true; d.profile.allow_software_fallback = false;
  d.policy = digitor::ExportExecutionPolicy::hardware_required;
  d.renderer_backend = DIGITOR_RENDERER_METAL;
  d.encoder_backend = digitor::EncoderBackend::video_toolbox;
  return std::make_shared<const digitor::ExportRenderSnapshot>(std::move(d));
}

inline void run_export_m5_apple_contract_cases() {
  using namespace digitor;
  auto snapshot = m5_snapshot();
  AppleHardwareEncodeQualification q{};
  std::uint64_t cancelled = 0;
  AppleHardwareEncoderHost host{};
  host.open = [&](const HardwareEncodeConfig& config, const ExportRenderSnapshot& frozen,
                  AppleHardwareEncodeCapabilities& caps, std::string&) {
    assert(config.backend == EncoderBackend::video_toolbox);
    assert(config.output_path == frozen.data().output_path);
    caps.platform = ApplePlatform::macos; caps.available = true;
    caps.hardware_accelerated = true; caps.iosurface_backed_pool = true;
    caps.h264 = true; caps.hevc = true; caps.prores = true;
    caps.ten_bit = true; caps.hdr_metadata = true; caps.alpha = true;
    caps.mp4 = true; caps.mov = true; caps.max_width = 8192; caps.max_height = 8192;
    caps.device_identity = "test-metal-device"; q.adapter_opened = true;
    return DIGITOR_RESULT_OK;
  };
  host.submit = [&](const AppleHardwareEncodeFrameDescriptor& f, std::string&) {
    assert(f.frame && f.frame->backend() == DIGITOR_RENDERER_METAL);
    q.metal_completion_waited = true;
    q.iosurface_pixel_buffer_acquired = true;
    q.pixel_buffer_pool_reused = true;
    q.attachments_propagated = true;
    ++q.submitted_frames; ++q.encoded_frames;
    return DIGITOR_RESULT_OK;
  };
  host.drain = [&](std::string&) { q.bitstream_produced = true; return DIGITOR_RESULT_OK; };
  host.finalize_atomic = [&](std::string&) { q.atomic_output_finalized = true; return DIGITOR_RESULT_OK; };
  host.cancel = [&] { ++cancelled; };
  host.qualification = [&] { return q; };

  auto adapter = create_apple_hardware_encode_adapter(snapshot, host);
  HardwareEncodeConfig config{};
  config.profile = snapshot->data().profile;
  config.backend = EncoderBackend::video_toolbox;
  config.output_path = snapshot->data().output_path;
  config.duration_us = snapshot->data().duration_us;
  config.require_hardware = true; config.require_zero_copy = true;
  config.require_atomic_finalize = true;
  ProductionHardwareEncodeSession session(config, adapter.callbacks);
  assert(session.start() == DIGITOR_RESULT_OK);
  assert(session.submit({m5_frame(0), 0, 33333, true}) == DIGITOR_RESULT_OK);
  assert(session.submit({m5_frame(33333), 33333, 33333, false}) == DIGITOR_RESULT_OK);
  assert(session.finish() == DIGITOR_RESULT_OK);
  const auto qualified = adapter.qualification();
  assert(qualified.metal_completion_waited && qualified.iosurface_pixel_buffer_acquired);
  assert(qualified.attachments_propagated && qualified.bitstream_produced);
  assert(qualified.atomic_output_finalized && qualified.no_cpu_readback);
  assert(qualified.no_cpu_staging && qualified.encoded_frames == 2);

  auto bad = snapshot->data(); bad.output_path = "bad.mkv";
  AppleHardwareEncodeCapabilities caps{};
  caps.available = true; caps.hardware_accelerated = true; caps.iosurface_backed_pool = true;
  caps.hevc = true; caps.ten_bit = true; caps.mov = true;
  caps.max_width = 8192; caps.max_height = 8192; caps.device_identity = "device";
  assert(!validate_apple_hardware_encode_contract(ExportRenderSnapshot(std::move(bad)), caps));

  auto violating = host;
  AppleHardwareEncodeQualification bad_q = q; bad_q.no_cpu_staging = false;
  violating.qualification = [&] { return bad_q; };
  auto violating_adapter = create_apple_hardware_encode_adapter(snapshot, violating);
  ProductionHardwareEncodeSession failed(config, violating_adapter.callbacks);
  assert(failed.start() == DIGITOR_RESULT_OK);
  assert(failed.submit({m5_frame(0), 0, 33333, true}) != DIGITOR_RESULT_OK);
  assert(cancelled >= 1);
}

}  // namespace
