#include "digitor/production_export.hpp"
#include "digitor/export_render_snapshot.hpp"
#include "digitor/gpu_frame.hpp"
#include "digitor/production_export_c_api.h"

#include <cassert>
#include <filesystem>
#include <vector>

int main() {
  namespace fs = std::filesystem;
  using namespace digitor;

  // Regression: float precision is frozen separately from the canonical
  // linear-RGBA color identity. Android GLES color passes produce RGBA32F
  // frames tagged "linear-rgba"; that must validate for production export.
  ExportRenderSnapshotData contract_data{};
  contract_data.snapshot_identity = 1;
  contract_data.timeline_revision = 1;
  contract_data.render_revision = 1;
  contract_data.node_graph_revision = 1;
  contract_data.color_pipeline_revision = 1;
  contract_data.audio_revision = 1;
  contract_data.width = 1920;
  contract_data.height = 1080;
  contract_data.working_format = DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT;
  contract_data.fps_num = 30;
  contract_data.fps_den = 1;
  contract_data.duration_us = 1'000'000;
  contract_data.color_metadata = "linear-rgba";
  contract_data.output_path = "contract.mp4";
  contract_data.profile.codec = ExportCodec::h264;
  contract_data.profile.width = 1920;
  contract_data.profile.height = 1080;
  contract_data.profile.fps_num = 30;
  contract_data.profile.fps_den = 1;
  contract_data.profile.video_bitrate = 12'000'000;
  contract_data.profile.prefer_hardware = true;
  contract_data.profile.allow_software_fallback = false;
  contract_data.policy = ExportExecutionPolicy::hardware_required;
  contract_data.renderer_backend = DIGITOR_RENDERER_OPENGL_ES;
  contract_data.encoder_backend = EncoderBackend::media_codec;
  const ExportRenderSnapshot contract_snapshot(contract_data);
  const auto owner = std::make_shared<int>(1);
  const auto ready = std::make_shared<std::atomic_bool>(true);
  const auto contract_frame = std::make_shared<ProcessedGpuFrame>(
      reinterpret_cast<void*>(0x1), DIGITOR_RENDERER_OPENGL_ES,
      GpuFrameMetadata{1920, 1080, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                       GpuFrameAlpha::straight, 0, "linear-rgba"},
      1, owner, ready, true);
  assert(validate_frame_against_snapshot(contract_snapshot, *contract_frame));

  const ExportProfile profile{.codec = ExportCodec::hevc,
                              .width = 3840,
                              .height = 2160,
                              .ten_bit = true};
  const std::vector<EncoderCapability> capabilities{
      {.backend = EncoderBackend::software,
       .codecs = {ExportCodec::h264, ExportCodec::hevc},
       .max_width = 7680,
       .max_height = 4320,
       .ten_bit = true,
       .hardware = false},
      {.backend = EncoderBackend::nvenc,
       .codecs = {ExportCodec::h264, ExportCodec::hevc},
       .max_width = 7680,
       .max_height = 4320,
       .ten_bit = true,
       .hardware = true},
  };
  const auto selected = EncoderSelector::select(profile, capabilities);
  assert(selected.supported);
  assert(selected.backend == EncoderBackend::nvenc);
  assert(!selected.used_fallback);

  auto software_only = profile;
  const std::vector<EncoderCapability> software_capability{capabilities.front()};
  const auto fallback = EncoderSelector::select(software_only, software_capability);
  assert(fallback.supported);
  assert(fallback.backend == EncoderBackend::software);
  assert(fallback.used_fallback);

  const auto root = fs::temp_directory_path() / "digitor-production-export-test";
  std::error_code error;
  fs::remove_all(root, error);
  PersistentArtifactCache cache(root / "cache", 8);
  assert(cache.store("clip-a", "frame-1", {1, 2, 3, 4}));
  assert(cache.store("clip-a", "frame-2", {5, 6, 7, 8}));
  const auto loaded = cache.load("clip-a", "frame-2");
  assert(loaded && loaded->size() == 4 && loaded->front() == 5);
  assert(cache.size_bytes() <= 8);
  assert(cache.erase_namespace("clip-a"));

  ProxyRequest proxy{.clip_id = "clip-a",
                     .source_path = "source.mp4",
                     .proxy_path = "proxy.mp4"};
  assert(validate_proxy_request(proxy));
  assert(make_proxy_cache_key(proxy, 7).find("clip-a-7") != std::string::npos);

  const auto checkpoint_path = root / "checkpoints" / "export.chk";
  ExportCheckpoint checkpoint{.project_id = "project-1",
                              .output_path = "output.mp4",
                              .duration_us = 10'000'000,
                              .timeline_revision = 4,
                              .render_revision = 9,
                              .backend = EncoderBackend::nvenc};
  ResumableExportSession session(checkpoint, checkpoint_path);
  assert(session.start());
  assert(session.advance(2'500'000));
  assert(session.pause());
  assert(session.snapshot().progress == 0.25);
  assert(session.resume());
  assert(session.advance(10'000'000));
  assert(session.snapshot().state == ExportState::completed);

  const auto restored = ResumableExportSession::load_checkpoint(checkpoint_path);
  assert(restored);
  assert(restored->completed_us == restored->duration_us);
  assert(restored->timeline_revision == 4);

  const auto c_checkpoint = (root / "checkpoints" / "c-export.chk").string();
  DigitorExportSession* c_session = digitor_export_session_create(
      "project-c", "output-c.mp4", c_checkpoint.c_str(), 1'000'000, 2, 3);
  assert(c_session);
  assert(digitor_export_session_start(c_session) == 1);
  assert(digitor_export_session_advance(c_session, 500'000) == 1);
  DigitorExportSnapshot snapshot{};
  assert(digitor_export_session_snapshot(c_session, &snapshot) == 1);
  assert(snapshot.state == DIGITOR_EXPORT_RUNNING);
  assert(snapshot.progress == 0.5);
  assert(digitor_export_session_cancel(c_session) == 1);
  digitor_export_session_destroy(c_session);

  fs::remove_all(root, error);
  return 0;
}
