#pragma once

#include "digitor/production_export.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace digitor {

struct FfmpegProgressSnapshot {
  std::int64_t frame{};
  std::int64_t out_time_us{};
  std::int64_t total_size{};
  double speed{};
  bool completed{};
};

class FfmpegProgressParser final {
 public:
  bool consume_line(const std::string& line) noexcept;
  [[nodiscard]] const FfmpegProgressSnapshot& snapshot() const noexcept;

 private:
  FfmpegProgressSnapshot snapshot_{};
};

struct ExportSegment {
  std::size_t index{};
  std::int64_t start_us{};
  std::int64_t duration_us{};
  std::filesystem::path output_path;
  bool completed{};
};

struct SegmentExportManifest {
  std::string project_id;
  std::filesystem::path input_path;
  std::filesystem::path output_path;
  std::filesystem::path working_directory;
  std::int64_t duration_us{};
  std::int64_t segment_duration_us{5'000'000};
  std::uint64_t timeline_revision{};
  std::uint64_t render_revision{};
  ExportProfile profile{};
  std::vector<ExportSegment> segments;
};

struct SegmentExportSnapshot {
  ExportState state{ExportState::idle};
  std::size_t total_segments{};
  std::size_t completed_segments{};
  std::size_t active_segment{};
  std::int64_t completed_us{};
  std::int64_t duration_us{};
  double progress{};
  std::uint64_t generation{};
  std::string diagnostic;
};

using SegmentCommandExecutor = std::function<int(
    const std::vector<std::string>&,
    const std::function<bool(const std::string&)>&)>;

class ResumableSegmentExport final {
 public:
  ResumableSegmentExport(SegmentExportManifest manifest,
                         std::filesystem::path manifest_path,
                         std::string ffmpeg_binary = "ffmpeg",
                         SegmentCommandExecutor executor = {});

  [[nodiscard]] static SegmentExportManifest plan(
      std::string project_id,
      std::filesystem::path input_path,
      std::filesystem::path output_path,
      std::filesystem::path working_directory,
      std::int64_t duration_us,
      std::int64_t segment_duration_us,
      ExportProfile profile,
      std::uint64_t timeline_revision = 0,
      std::uint64_t render_revision = 0);

  [[nodiscard]] bool run();
  void request_cancel() noexcept;
  [[nodiscard]] bool persist() const;
  [[nodiscard]] SegmentExportSnapshot snapshot() const;
  [[nodiscard]] const SegmentExportManifest& manifest() const noexcept;

  [[nodiscard]] static std::optional<SegmentExportManifest> load(
      const std::filesystem::path& manifest_path);

 private:
  [[nodiscard]] bool render_segment(ExportSegment& segment);
  [[nodiscard]] bool concatenate_segments();
  void update_progress(std::size_t active_index,
                       const FfmpegProgressSnapshot& progress);

  SegmentExportManifest manifest_;
  std::filesystem::path manifest_path_;
  std::string ffmpeg_binary_;
  SegmentCommandExecutor executor_;
  SegmentExportSnapshot snapshot_{};
  bool cancel_requested_{};
};

}  // namespace digitor
