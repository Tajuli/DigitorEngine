#include "digitor/professional_import.hpp"

#include <cassert>
#include <set>
#include <string>
#include <unordered_map>

using namespace digitor;

int main() {
  std::set<std::string> existing{"/media/A001.mov", "/media/A001-proxy.mov",
                                 "/relinked/A001.mov"};
  std::unordered_map<std::string, std::uint64_t> sizes{{"/media/A001.mov", 1000},
                                                       {"/media/A001-proxy.mov", 500},
                                                       {"/relinked/A001.mov", 1000}};
  std::unordered_map<std::string, std::int64_t> modified{{"/media/A001.mov", 10},
                                                         {"/media/A001-proxy.mov", 11},
                                                         {"/relinked/A001.mov", 10}};
  bool persisted = false;

  ProfessionalImportCallbacks callbacks;
  callbacks.exists = [&](const std::string& path) { return existing.contains(path); };
  callbacks.file_size = [&](const std::string& path) { return sizes.at(path); };
  callbacks.modified_time_ns = [&](const std::string& path) { return modified.at(path); };
  callbacks.canonicalize = [](const std::string& path) { return path; };
  callbacks.fingerprint = [](const std::string& path, std::uint64_t size, std::int64_t mtime) {
    const auto name = path.ends_with("A001.mov") ? "A001.mov" : path;
    return name + ":" + std::to_string(size) + ":" + std::to_string(mtime);
  };
  callbacks.probe = [](const std::string&, MediaProbeResult& out, std::string&) {
    out.kind = MediaAssetKind::video;
    out.container = "mov";
    out.codec = "prores";
    out.duration_us = 5'000'000;
    out.width = 3840;
    out.height = 2160;
    out.frame_rate_num = 24000;
    out.frame_rate_den = 1001;
    out.bit_depth = 10;
    out.chroma_subsampling = "4:2:2";
    out.color_primaries = "bt2020";
    out.transfer = "smpte2084";
    out.matrix = "bt2020nc";
    out.color_range = "limited";
    out.hdr_metadata = "HDR10";
    out.timecode = "01:00:00:00";
    out.streams.push_back({0, "video", "prores", {}, 3840, 2160, 10, "yuv422p10le",
                           {}, 0, 0, true});
    out.streams.push_back({1, "audio", "pcm_s24le", "eng", 0, 0, 24, {}, "stereo",
                           48000, 2, true});
    out.streams.push_back({2, "subtitle", "mov_text", "eng", 0, 0, 0, {}, {}, 0, 0,
                           false});
    return DIGITOR_RESULT_OK;
  };
  callbacks.generate_thumbnail = [](const MediaAsset& asset, std::string& out, std::string&) {
    out = asset.id + ".thumb";
    return DIGITOR_RESULT_OK;
  };
  callbacks.generate_waveform = [](const MediaAsset& asset, std::string& out, std::string&) {
    out = asset.id + ".wave";
    return DIGITOR_RESULT_OK;
  };
  callbacks.generate_proxy = [](const MediaAsset&, std::string& out, std::string&) {
    out = "/media/A001-proxy.mov";
    return DIGITOR_RESULT_OK;
  };
  callbacks.generate_optimized_media = [](const MediaAsset& asset, std::string& out,
                                           std::string&) {
    out = asset.id + ".optimized.mov";
    return DIGITOR_RESULT_OK;
  };
  callbacks.persist = [&](const std::vector<MediaAsset>& assets,
                          const std::vector<MediaBin>& bins, std::string&) {
    persisted = assets.size() == 1 && !bins.empty();
    return persisted ? DIGITOR_RESULT_OK : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  };

  ProfessionalImportEngine engine(std::move(callbacks));
  const auto rushes = engine.create_bin("Rushes");
  ImportOptions options;
  options.bin_id = rushes;
  options.create_proxy = true;
  options.create_optimized_media = true;
  const auto imported = engine.import_path("/media/A001.mov", options);
  assert(imported.result == DIGITOR_RESULT_OK);
  assert(!imported.asset_id.empty());

  const auto asset = engine.asset(imported.asset_id);
  assert(asset);
  assert(asset->probe.streams.size() == 3);
  assert(asset->probe.bit_depth == 10);
  assert(asset->thumbnail_path.ends_with(".thumb"));
  assert(asset->waveform_path.ends_with(".wave"));
  assert(asset->proxy_path == "/media/A001-proxy.mov");
  assert(!asset->optimized_media_path.empty());
  assert(asset->bin_id == rushes);

  const auto duplicate = engine.import_path("/media/A001.mov", options);
  assert(duplicate.result == DIGITOR_RESULT_OK);
  assert(duplicate.duplicate);
  assert(duplicate.asset_id == imported.asset_id);

  assert(engine.add_tag(imported.asset_id, "interview"));
  assert(engine.search("INTERVIEW").size() == 1);
  assert(engine.set_timeline_reference_count(imported.asset_id, 3));
  assert(engine.asset(imported.asset_id)->timeline_reference_count == 3);

  existing.erase("/media/A001.mov");
  engine.refresh_online_state();
  assert(engine.asset(imported.asset_id)->state == MediaOnlineState::offline);
  assert(engine.relink_search({"/relinked/A001.mov"}) == 1);
  assert(engine.asset(imported.asset_id)->state == MediaOnlineState::online);
  assert(engine.asset(imported.asset_id)->original_path == "/relinked/A001.mov");

  std::string diagnostic;
  assert(engine.persist(&diagnostic) == DIGITOR_RESULT_OK);
  assert(persisted);

  const auto sequence = ProfessionalImportEngine::detect_sequence(
      "/seq/shot_0001.exr",
      {"/seq/shot_0001.exr", "/seq/shot_0002.exr", "/seq/shot_0004.exr"}, 24, 1);
  assert(sequence.detected);
  assert(sequence.first_frame == 1);
  assert(sequence.last_frame == 4);
  assert(sequence.missing_frames.size() == 1 && sequence.missing_frames.front() == 3);

  const auto telemetry = engine.telemetry();
  assert(telemetry.imported_assets == 1);
  assert(telemetry.duplicate_imports == 1);
  assert(telemetry.relinked_assets == 1);
  assert(telemetry.completed_jobs == 4);
  assert(telemetry.failed_jobs == 0);
  return 0;
}
