#include "digitor/production_export.hpp"
#include "digitor/production_export_c_api.h"

#include <cassert>
#include <filesystem>
#include <vector>

int main() {
  namespace fs = std::filesystem;
  using namespace digitor;

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
