#include "digitor/resumable_segment_export.hpp"

#include "digitor/ffmpeg_export_runtime.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace digitor {
namespace {
int unavailable_executor(const std::vector<std::string>&,
                         const std::function<bool(const std::string&)>&) {
  return -1;
}

std::string value_after_equal(const std::string& line) {
  const auto pos = line.find('=');
  return pos == std::string::npos ? std::string{} : line.substr(pos + 1);
}

template <typename T>
bool parse_integer(const std::string& text, T& out) noexcept {
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto result = std::from_chars(begin, end, out);
  return result.ec == std::errc{} && result.ptr == end;
}

std::string codec_token(ExportCodec codec) {
  switch (codec) {
    case ExportCodec::h264: return "h264";
    case ExportCodec::hevc: return "hevc";
    case ExportCodec::av1: return "av1";
    case ExportCodec::prores: return "prores";
  }
  return "h264";
}
}  // namespace

bool FfmpegProgressParser::consume_line(const std::string& line) noexcept {
  if (line.rfind("frame=", 0) == 0) return parse_integer(value_after_equal(line), snapshot_.frame);
  if (line.rfind("out_time_us=", 0) == 0) return parse_integer(value_after_equal(line), snapshot_.out_time_us);
  if (line.rfind("total_size=", 0) == 0) return parse_integer(value_after_equal(line), snapshot_.total_size);
  if (line.rfind("speed=", 0) == 0) {
    auto value = value_after_equal(line);
    if (!value.empty() && value.back() == 'x') value.pop_back();
    try {
      snapshot_.speed = std::stod(value);
      return true;
    } catch (...) {
      return false;
    }
  }
  if (line == "progress=end") {
    snapshot_.completed = true;
    return true;
  }
  return line == "progress=continue";
}

const FfmpegProgressSnapshot& FfmpegProgressParser::snapshot() const noexcept {
  return snapshot_;
}

ResumableSegmentExport::ResumableSegmentExport(SegmentExportManifest manifest,
                                               std::filesystem::path manifest_path,
                                               std::string ffmpeg_binary,
                                               SegmentCommandExecutor executor)
    : manifest_(std::move(manifest)),
      manifest_path_(std::move(manifest_path)),
      ffmpeg_binary_(std::move(ffmpeg_binary)),
      executor_(executor ? std::move(executor) : unavailable_executor) {
  snapshot_.duration_us = manifest_.duration_us;
  snapshot_.total_segments = manifest_.segments.size();
  snapshot_.diagnostic = "idle";
  for (const auto& segment : manifest_.segments) {
    if (segment.completed && std::filesystem::exists(segment.output_path)) {
      ++snapshot_.completed_segments;
      snapshot_.completed_us += segment.duration_us;
    }
  }
  snapshot_.progress = snapshot_.duration_us > 0
                           ? static_cast<double>(snapshot_.completed_us) /
                                 static_cast<double>(snapshot_.duration_us)
                           : 0.0;
}

SegmentExportManifest ResumableSegmentExport::plan(
    std::string project_id,
    std::filesystem::path input_path,
    std::filesystem::path output_path,
    std::filesystem::path working_directory,
    std::int64_t duration_us,
    std::int64_t segment_duration_us,
    ExportProfile profile,
    std::uint64_t timeline_revision,
    std::uint64_t render_revision) {
  SegmentExportManifest manifest;
  manifest.project_id = std::move(project_id);
  manifest.input_path = std::move(input_path);
  manifest.output_path = std::move(output_path);
  manifest.working_directory = std::move(working_directory);
  manifest.duration_us = std::max<std::int64_t>(duration_us, 0);
  manifest.segment_duration_us = std::max<std::int64_t>(segment_duration_us, 250'000);
  manifest.profile = profile;
  manifest.timeline_revision = timeline_revision;
  manifest.render_revision = render_revision;
  std::int64_t start = 0;
  std::size_t index = 0;
  while (start < manifest.duration_us) {
    const auto duration = std::min(manifest.segment_duration_us, manifest.duration_us - start);
    std::ostringstream name;
    name << "segment_" << std::setw(6) << std::setfill('0') << index << ".mp4";
    manifest.segments.push_back({index, start, duration, manifest.working_directory / name.str(), false});
    start += duration;
    ++index;
  }
  return manifest;
}

bool ResumableSegmentExport::run() {
  cancel_requested_ = false;
  snapshot_.state = ExportState::running;
  snapshot_.diagnostic = "rendering segments";
  ++snapshot_.generation;
  std::error_code ec;
  std::filesystem::create_directories(manifest_.working_directory, ec);
  for (auto& segment : manifest_.segments) {
    if (cancel_requested_) {
      snapshot_.state = ExportState::cancelled;
      snapshot_.diagnostic = "cancelled between segments";
      persist();
      return false;
    }
    if (segment.completed && std::filesystem::exists(segment.output_path)) continue;
    if (!render_segment(segment)) {
      if (cancel_requested_) {
        snapshot_.state = ExportState::cancelled;
        snapshot_.diagnostic = "cancelled";
      } else {
        snapshot_.state = ExportState::failed;
        snapshot_.diagnostic = "segment render failed";
      }
      persist();
      return false;
    }
    segment.completed = true;
    ++snapshot_.completed_segments;
    snapshot_.completed_us = std::min(manifest_.duration_us,
                                      snapshot_.completed_us + segment.duration_us);
    snapshot_.progress = manifest_.duration_us > 0
                             ? static_cast<double>(snapshot_.completed_us) /
                                   static_cast<double>(manifest_.duration_us)
                             : 1.0;
    ++snapshot_.generation;
    persist();
  }
  if (!concatenate_segments()) {
    snapshot_.state = ExportState::failed;
    snapshot_.diagnostic = "segment concatenation failed";
    persist();
    return false;
  }
  snapshot_.state = ExportState::completed;
  snapshot_.completed_us = manifest_.duration_us;
  snapshot_.progress = 1.0;
  snapshot_.diagnostic = "completed";
  ++snapshot_.generation;
  persist();
  return true;
}

bool ResumableSegmentExport::render_segment(ExportSegment& segment) {
  const auto temporary = segment.output_path.string() + ".partial";
  std::vector<std::string> args{
      ffmpeg_binary_, "-hide_banner", "-nostdin", "-y", "-ss",
      std::to_string(static_cast<double>(segment.start_us) / 1'000'000.0), "-t",
      std::to_string(static_cast<double>(segment.duration_us) / 1'000'000.0), "-i",
      manifest_.input_path.string(), "-c:v", ffmpeg_video_encoder(EncoderBackend::software, manifest_.profile.codec),
      "-b:v", std::to_string(manifest_.profile.video_bitrate), "-vf",
      "scale=w=" + std::to_string(manifest_.profile.width) + ":h=" +
          std::to_string(manifest_.profile.height) + ":force_original_aspect_ratio=decrease",
      "-c:a", "aac", "-progress", "pipe:1", "-nostats", temporary};
  FfmpegProgressParser parser;
  const auto exit_code = executor_(args, [&](const std::string& line) {
    parser.consume_line(line);
    update_progress(segment.index, parser.snapshot());
    return !cancel_requested_;
  });
  std::error_code ec;
  if (cancel_requested_ || exit_code != 0 || !std::filesystem::exists(temporary)) {
    std::filesystem::remove(temporary, ec);
    return false;
  }
  std::filesystem::remove(segment.output_path, ec);
  std::filesystem::rename(temporary, segment.output_path, ec);
  return !ec;
}

bool ResumableSegmentExport::concatenate_segments() {
  const auto list_path = manifest_.working_directory / "concat.txt";
  {
    std::ofstream list(list_path, std::ios::trunc);
    if (!list) return false;
    for (const auto& segment : manifest_.segments) {
      if (!segment.completed || !std::filesystem::exists(segment.output_path)) return false;
      list << "file '" << segment.output_path.generic_string() << "'\n";
    }
  }
  const auto temporary = manifest_.output_path.string() + ".partial";
  const std::vector<std::string> args{ffmpeg_binary_, "-hide_banner", "-nostdin", "-y",
                                      "-f", "concat", "-safe", "0", "-i",
                                      list_path.string(), "-c", "copy", temporary};
  const auto exit_code = executor_(args, [&](const std::string&) { return !cancel_requested_; });
  std::error_code ec;
  if (cancel_requested_ || exit_code != 0 || !std::filesystem::exists(temporary)) {
    std::filesystem::remove(temporary, ec);
    return false;
  }
  std::filesystem::remove(manifest_.output_path, ec);
  std::filesystem::rename(temporary, manifest_.output_path, ec);
  return !ec;
}

void ResumableSegmentExport::request_cancel() noexcept {
  cancel_requested_ = true;
  ++snapshot_.generation;
}

bool ResumableSegmentExport::persist() const {
  std::error_code ec;
  std::filesystem::create_directories(manifest_path_.parent_path(), ec);
  const auto temporary = manifest_path_.string() + ".tmp";
  std::ofstream out(temporary, std::ios::trunc);
  if (!out) return false;
  out << "DIGITOR_SEGMENT_EXPORT_V1\n";
  out << manifest_.project_id << '\n' << manifest_.input_path.string() << '\n'
      << manifest_.output_path.string() << '\n' << manifest_.working_directory.string() << '\n';
  out << manifest_.duration_us << ' ' << manifest_.segment_duration_us << ' '
      << manifest_.timeline_revision << ' ' << manifest_.render_revision << '\n';
  out << static_cast<int>(manifest_.profile.codec) << ' ' << manifest_.profile.width << ' '
      << manifest_.profile.height << ' ' << manifest_.profile.fps_num << ' '
      << manifest_.profile.fps_den << ' ' << manifest_.profile.video_bitrate << '\n';
  out << manifest_.segments.size() << '\n';
  for (const auto& segment : manifest_.segments) {
    out << segment.index << ' ' << segment.start_us << ' ' << segment.duration_us << ' '
        << (segment.completed ? 1 : 0) << ' ' << segment.output_path.string() << '\n';
  }
  out.close();
  if (!out) return false;
  std::filesystem::remove(manifest_path_, ec);
  std::filesystem::rename(temporary, manifest_path_, ec);
  return !ec;
}

std::optional<SegmentExportManifest> ResumableSegmentExport::load(
    const std::filesystem::path& manifest_path) {
  std::ifstream in(manifest_path);
  std::string magic;
  if (!std::getline(in, magic) || magic != "DIGITOR_SEGMENT_EXPORT_V1") return std::nullopt;
  SegmentExportManifest manifest;
  if (!std::getline(in, manifest.project_id)) return std::nullopt;
  std::string value;
  if (!std::getline(in, value)) return std::nullopt;
  manifest.input_path = value;
  if (!std::getline(in, value)) return std::nullopt;
  manifest.output_path = value;
  if (!std::getline(in, value)) return std::nullopt;
  manifest.working_directory = value;
  if (!(in >> manifest.duration_us >> manifest.segment_duration_us >> manifest.timeline_revision >> manifest.render_revision)) return std::nullopt;
  int codec = 0;
  if (!(in >> codec >> manifest.profile.width >> manifest.profile.height >> manifest.profile.fps_num >> manifest.profile.fps_den >> manifest.profile.video_bitrate)) return std::nullopt;
  manifest.profile.codec = static_cast<ExportCodec>(codec);
  std::size_t count = 0;
  if (!(in >> count)) return std::nullopt;
  manifest.segments.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    ExportSegment segment;
    int completed = 0;
    if (!(in >> segment.index >> segment.start_us >> segment.duration_us >> completed)) return std::nullopt;
    in >> std::ws;
    if (!std::getline(in, value)) return std::nullopt;
    segment.output_path = value;
    segment.completed = completed != 0;
    manifest.segments.push_back(std::move(segment));
  }
  return manifest;
}

void ResumableSegmentExport::update_progress(std::size_t active_index,
                                             const FfmpegProgressSnapshot& progress) {
  snapshot_.active_segment = active_index;
  const auto completed_before = static_cast<std::int64_t>(active_index) * manifest_.segment_duration_us;
  snapshot_.completed_us = std::clamp(completed_before + progress.out_time_us,
                                      std::int64_t{0}, manifest_.duration_us);
  snapshot_.progress = manifest_.duration_us > 0
                           ? static_cast<double>(snapshot_.completed_us) /
                                 static_cast<double>(manifest_.duration_us)
                           : 0.0;
  snapshot_.diagnostic = "segment " + std::to_string(active_index + 1) + "/" +
                         std::to_string(manifest_.segments.size()) + " speed=" +
                         std::to_string(progress.speed) + "x codec=" +
                         codec_token(manifest_.profile.codec);
}

SegmentExportSnapshot ResumableSegmentExport::snapshot() const { return snapshot_; }
const SegmentExportManifest& ResumableSegmentExport::manifest() const noexcept { return manifest_; }

}  // namespace digitor
