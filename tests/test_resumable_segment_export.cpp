#include "digitor/resumable_segment_export.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "qualification failure: " << message << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  using namespace digitor;

  FfmpegProgressParser parser;
  if (!require(parser.consume_line("frame=42"), "parse frame")) return 1;
  if (!require(parser.consume_line("out_time_us=1500000"), "parse time")) return 1;
  if (!require(parser.consume_line("speed=1.75x"), "parse speed")) return 1;
  if (!require(parser.consume_line("progress=end"), "parse completion")) return 1;
  if (!require(parser.snapshot().frame == 42, "frame snapshot")) return 1;
  if (!require(parser.snapshot().out_time_us == 1500000, "time snapshot")) return 1;
  if (!require(parser.snapshot().speed > 1.7, "speed snapshot")) return 1;
  if (!require(parser.snapshot().completed, "completion snapshot")) return 1;

  const auto root = std::filesystem::temp_directory_path() / "digitor_segment_export_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  ec.clear();
  std::filesystem::create_directories(root, ec);
  if (!require(!ec, "create test directory")) return 1;

  const auto input = root / "input.mp4";
  {
    std::ofstream source(input, std::ios::binary | std::ios::trunc);
    source << "source";
    if (!require(static_cast<bool>(source), "create source fixture")) return 1;
  }

  ExportProfile profile;
  profile.width = 320;
  profile.height = 180;
  profile.video_bitrate = 500000;
  auto manifest = ResumableSegmentExport::plan(
      "project", input, root / "final.mp4", root / "work", 12000000, 5000000,
      profile, 7, 9);
  if (!require(manifest.segments.size() == 3, "segment count")) return 1;
  if (!require(manifest.segments[2].duration_us == 2000000, "tail duration")) return 1;

  std::size_t invocations = 0;
  auto executor = [&](const std::vector<std::string>& args,
                      const std::function<bool(const std::string&)>& progress) {
    ++invocations;
    if (args.empty()) return 90;
    if (!progress("out_time_us=1000000")) return 91;
    if (!progress("speed=2.0x")) return 92;
    const auto output = std::filesystem::path(args.back());
    std::error_code local_ec;
    if (!output.parent_path().empty()) {
      std::filesystem::create_directories(output.parent_path(), local_ec);
      if (local_ec) return 93;
    }
    std::ofstream encoded(output, std::ios::binary | std::ios::trunc);
    encoded << "encoded";
    return encoded ? 0 : 94;
  };

  const auto manifest_path = root / "checkpoint.manifest";
  ResumableSegmentExport session(manifest, manifest_path, "ffmpeg", executor);
  const bool first_run_ok = session.run();
  if (!require(first_run_ok, "initial segmented export")) return 1;
  const auto first_snapshot = session.snapshot();
  if (!require(first_snapshot.state == ExportState::completed, "completed state")) return 1;
  if (!require(std::abs(first_snapshot.progress - 1.0) < 0.000001, "completed progress")) return 1;
  if (!require(std::filesystem::exists(root / "final.mp4"), "final output")) return 1;
  if (!require(invocations == 4, "three segments plus concat")) return 1;

  const auto loaded = ResumableSegmentExport::load(manifest_path);
  if (!require(loaded.has_value(), "load checkpoint")) return 1;
  if (!require(loaded->segments.size() == 3, "loaded segment count")) return 1;
  for (const auto& segment : loaded->segments) {
    if (!require(segment.completed, "loaded completed segment")) return 1;
  }

  std::size_t resumed_invocations = 0;
  auto resume_executor = [&](const std::vector<std::string>& args,
                             const std::function<bool(const std::string&)>&) {
    ++resumed_invocations;
    if (args.empty()) return 95;
    const auto output = std::filesystem::path(args.back());
    std::ofstream concat(output, std::ios::binary | std::ios::trunc);
    concat << "concat";
    return concat ? 0 : 96;
  };
  ResumableSegmentExport resumed(*loaded, manifest_path, "ffmpeg", resume_executor);
  const bool resume_ok = resumed.run();
  if (!require(resume_ok, "concat-only resume")) return 1;
  if (!require(resumed_invocations == 1, "completed segments reused")) return 1;

  auto cancelled_manifest = ResumableSegmentExport::plan(
      "cancel", input, root / "cancel.mp4", root / "cancel_work", 5000000,
      5000000, profile);
  ResumableSegmentExport cancelled(cancelled_manifest, root / "cancel.manifest",
                                    "ffmpeg", executor);
  cancelled.request_cancel();
  const bool cancelled_ok = cancelled.run();
  if (!require(!cancelled_ok, "cancelled run returns false")) return 1;
  if (!require(cancelled.snapshot().state == ExportState::cancelled,
               "cancelled state")) return 1;

  std::filesystem::remove_all(root, ec);
  return 0;
}
