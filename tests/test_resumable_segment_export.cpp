#include "digitor/resumable_segment_export.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main() {
  using namespace digitor;
  FfmpegProgressParser parser;
  assert(parser.consume_line("frame=42"));
  assert(parser.consume_line("out_time_us=1500000"));
  assert(parser.consume_line("speed=1.75x"));
  assert(parser.consume_line("progress=end"));
  assert(parser.snapshot().frame == 42);
  assert(parser.snapshot().out_time_us == 1500000);
  assert(parser.snapshot().speed > 1.7);
  assert(parser.snapshot().completed);

  const auto root = std::filesystem::temp_directory_path() / "digitor_segment_export_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  const auto input = root / "input.mp4";
  std::ofstream(input) << "source";

  ExportProfile profile;
  profile.width = 320;
  profile.height = 180;
  profile.video_bitrate = 500000;
  auto manifest = ResumableSegmentExport::plan(
      "project", input, root / "final.mp4", root / "work", 12000000, 5000000,
      profile, 7, 9);
  assert(manifest.segments.size() == 3);
  assert(manifest.segments[2].duration_us == 2000000);

  std::size_t invocations = 0;
  auto executor = [&](const std::vector<std::string>& args,
                      const std::function<bool(const std::string&)>& progress) {
    ++invocations;
    assert(progress("out_time_us=1000000"));
    assert(progress("speed=2.0x"));
    const auto output = std::filesystem::path(args.back());
    std::filesystem::create_directories(output.parent_path(), ec);
    std::ofstream(output, std::ios::binary) << "encoded";
    return 0;
  };

  const auto manifest_path = root / "checkpoint.manifest";
  ResumableSegmentExport session(manifest, manifest_path, "ffmpeg", executor);
  assert(session.run());
  assert(session.snapshot().state == ExportState::completed);
  assert(session.snapshot().progress == 1.0);
  assert(std::filesystem::exists(root / "final.mp4"));
  assert(invocations == 4);

  const auto loaded = ResumableSegmentExport::load(manifest_path);
  assert(loaded.has_value());
  assert(loaded->segments.size() == 3);
  for (const auto& segment : loaded->segments) assert(segment.completed);

  std::size_t resumed_invocations = 0;
  auto resume_executor = [&](const std::vector<std::string>& args,
                             const std::function<bool(const std::string&)>&) {
    ++resumed_invocations;
    const auto output = std::filesystem::path(args.back());
    std::ofstream(output, std::ios::binary) << "concat";
    return 0;
  };
  ResumableSegmentExport resumed(*loaded, manifest_path, "ffmpeg", resume_executor);
  assert(resumed.run());
  assert(resumed_invocations == 1);

  std::filesystem::remove_all(root, ec);
  return 0;
}
