#include "digitor/export_render_snapshot.hpp"
#include "digitor/production_hardware_encode.hpp"
#include "export_m2_contract_cases.hpp"

#include <atomic>
#include <cassert>
#include <memory>
#include <string>
#include <type_traits>

using namespace digitor;

namespace {
ProcessedGpuFramePtr make_frame(std::int64_t pts, std::uint64_t identity = 1,
                                DigitorRendererBackend backend = DIGITOR_RENDERER_D3D12) {
  static int context;
  GpuFrameMetadata metadata{};
  metadata.width = 1920;
  metadata.height = 1080;
  metadata.format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  metadata.timestamp = pts;
  metadata.color_metadata = "linear-rgba";
  return std::make_shared<ProcessedGpuFrame>(
      &context, backend, metadata, identity,
      std::static_pointer_cast<void>(std::make_shared<int>(1)),
      std::make_shared<std::atomic_bool>(true), false);
}

HardwareEncodeConfig config() {
  HardwareEncodeConfig value{};
  value.backend = EncoderBackend::nvenc;
  value.output_path = "output.mp4";
  value.duration_us = 100000;
  value.profile.width = 1920;
  value.profile.height = 1080;
  value.profile.fps_num = 30;
  value.profile.fps_den = 1;
  value.profile.codec = ExportCodec::h264;
  value.profile.ten_bit = true;
  return value;
}

ExportRenderSnapshotData hardware_snapshot_data() {
  ExportRenderSnapshotData value{};
  value.snapshot_identity = 1001;
  value.timeline_revision = 11;
  value.render_revision = 12;
  value.node_graph_revision = 13;
  value.color_pipeline_revision = 14;
  value.audio_revision = 15;
  value.width = 1920;
  value.height = 1080;
  value.working_format = DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT;
  value.fps_num = 30;
  value.fps_den = 1;
  value.duration_us = 100000;
  value.color_metadata = "linear-rgba";
  value.output_path = "output.mp4";
  value.profile.width = 1920;
  value.profile.height = 1080;
  value.profile.fps_num = 30;
  value.profile.fps_den = 1;
  value.profile.prefer_hardware = true;
  value.profile.allow_software_fallback = false;
  value.policy = ExportExecutionPolicy::hardware_required;
  value.renderer_backend = DIGITOR_RENDERER_D3D12;
  value.encoder_backend = EncoderBackend::nvenc;
  return value;
}
}  // namespace

int main() {
  static_assert(!std::is_copy_assignable_v<ExportRenderSnapshot>);
  static_assert(!std::is_move_assignable_v<ExportRenderSnapshot>);

  const ExportRenderSnapshot frozen(hardware_snapshot_data());
  assert(frozen.identity() == 1001);
  assert(export_policy_uses_gpu(frozen.policy()));
  assert(validate_export_snapshot(frozen));
  assert(validate_frame_against_snapshot(frozen, *make_frame(0)));

  auto hidden_fallback = hardware_snapshot_data();
  hidden_fallback.profile.allow_software_fallback = true;
  assert(!validate_export_snapshot(ExportRenderSnapshot(std::move(hidden_fallback))));

  auto mixed_renderer = hardware_snapshot_data();
  mixed_renderer.renderer_backend = DIGITOR_RENDERER_CPU;
  assert(!validate_export_snapshot(ExportRenderSnapshot(std::move(mixed_renderer))));

  auto mixed_encoder = hardware_snapshot_data();
  mixed_encoder.encoder_backend = EncoderBackend::software;
  assert(!validate_export_snapshot(ExportRenderSnapshot(std::move(mixed_encoder))));

  auto mismatched_profile = hardware_snapshot_data();
  mismatched_profile.profile.width = 1280;
  assert(!validate_export_snapshot(ExportRenderSnapshot(std::move(mismatched_profile))));

  auto missing_output = hardware_snapshot_data();
  missing_output.output_path.clear();
  assert(!validate_export_snapshot(ExportRenderSnapshot(std::move(missing_output))));

  assert(!validate_frame_against_snapshot(frozen,
                                          *make_frame(0, 9, DIGITOR_RENDERER_VULKAN)));
  assert(!validate_frame_against_snapshot(frozen,
                                          *make_frame(0, 10, DIGITOR_RENDERER_CPU)));

  auto cpu_data = hardware_snapshot_data();
  cpu_data.policy = ExportExecutionPolicy::explicit_cpu_reference;
  cpu_data.renderer_backend = DIGITOR_RENDERER_CPU;
  cpu_data.encoder_backend = EncoderBackend::software;
  cpu_data.profile.prefer_hardware = false;
  cpu_data.profile.allow_software_fallback = true;
  const ExportRenderSnapshot cpu_snapshot(std::move(cpu_data));
  assert(validate_export_snapshot(cpu_snapshot));
  assert(!export_policy_uses_gpu(cpu_snapshot.policy()));

  std::uint64_t opened = 0;
  std::uint64_t submitted = 0;
  std::uint64_t drained = 0;
  std::uint64_t finalized = 0;
  std::uint64_t cancelled = 0;

  HardwareEncoderCallbacks callbacks{};
  callbacks.open = [&](const HardwareEncodeConfig& value, std::string&) {
    assert(value.backend == EncoderBackend::nvenc);
    ++opened;
    return DIGITOR_RESULT_OK;
  };
  callbacks.submit_gpu_frame = [&](const HardwareEncodeFrame& value, std::string&) {
    assert(value.frame && value.frame->backend() == DIGITOR_RENDERER_D3D12);
    ++submitted;
    return DIGITOR_RESULT_OK;
  };
  callbacks.drain = [&](std::string&) { ++drained; return DIGITOR_RESULT_OK; };
  callbacks.finalize_atomic = [&](std::string&) { ++finalized; return DIGITOR_RESULT_OK; };
  callbacks.cancel = [&] { ++cancelled; };

  ProductionHardwareEncodeSession session(config(), callbacks);
  std::string diagnostic;
  assert(session.start(&diagnostic) == DIGITOR_RESULT_OK);
  assert(diagnostic.empty());
  assert(session.submit({make_frame(0), 0, 33333, true}, &diagnostic) == DIGITOR_RESULT_OK);
  assert(session.submit({make_frame(33333, 2), 33333, 33333, false}, &diagnostic) == DIGITOR_RESULT_OK);
  assert(session.submit({make_frame(66666, 3), 66666, 33334, false}, &diagnostic) == DIGITOR_RESULT_OK);
  assert(session.finish(&diagnostic) == DIGITOR_RESULT_OK);

  const auto telemetry = session.telemetry();
  assert(telemetry.state == HardwareEncodeState::completed);
  assert(telemetry.submitted_frames == 3);
  assert(telemetry.accepted_frames == 3);
  assert(telemetry.rejected_frames == 0);
  assert(telemetry.cpu_readbacks == 0);
  assert(telemetry.progress == 1.0);
  assert(opened == 1 && submitted == 3 && drained == 1 && finalized == 1);
  assert(cancelled == 0);

  ProductionHardwareEncodeSession timestamps(config(), callbacks);
  assert(timestamps.start() == DIGITOR_RESULT_OK);
  assert(timestamps.submit({make_frame(100, 4), 100, 33, false}) == DIGITOR_RESULT_OK);
  assert(timestamps.submit({make_frame(100, 5), 100, 33, false}) != DIGITOR_RESULT_OK);
  assert(timestamps.telemetry().state == HardwareEncodeState::failed);

  auto software = config();
  software.backend = EncoderBackend::software;
  ProductionHardwareEncodeSession strict_software(software, callbacks);
  assert(strict_software.start() != DIGITOR_RESULT_OK);

  ProductionHardwareEncodeSession cancelled_session(config(), callbacks);
  assert(cancelled_session.start() == DIGITOR_RESULT_OK);
  cancelled_session.cancel();
  assert(cancelled_session.telemetry().state == HardwareEncodeState::cancelled);
  assert(cancelled == 1);

  run_export_m2_contract_cases();
  return 0;
}
