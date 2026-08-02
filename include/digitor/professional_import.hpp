#pragma once

#include "digitor/digitor.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class MediaAssetKind { video, audio, still_image, image_sequence, subtitle, unknown };
enum class MediaOnlineState { online, offline, changed };
enum class ImportJobKind { probe, thumbnail, waveform, proxy, optimized_media };
enum class ImportJobState { queued, running, completed, failed, cancelled };

struct MediaStreamInfo {
  int index{-1};
  std::string type;
  std::string codec;
  std::string language;
  int width{};
  int height{};
  int bit_depth{};
  std::string pixel_format;
  std::string channel_layout;
  int sample_rate{};
  int channels{};
  bool selected{};
};

struct MediaProbeResult {
  MediaAssetKind kind{MediaAssetKind::unknown};
  std::string container;
  std::string codec;
  std::int64_t duration_us{};
  int width{};
  int height{};
  int frame_rate_num{};
  int frame_rate_den{1};
  bool variable_frame_rate{};
  int bit_depth{};
  std::string chroma_subsampling;
  std::string color_primaries;
  std::string transfer;
  std::string matrix;
  std::string color_range;
  std::string hdr_metadata;
  int rotation_degrees{};
  std::string timecode;
  std::vector<MediaStreamInfo> streams;
};

struct ImageSequenceInfo {
  bool detected{};
  std::string prefix;
  std::string extension;
  int first_frame{};
  int last_frame{};
  int padding{};
  std::vector<int> missing_frames;
  int frame_rate_num{24};
  int frame_rate_den{1};
};

struct MediaAsset {
  std::string id;
  std::string original_path;
  std::string canonical_path;
  std::string display_name;
  std::string bin_id;
  std::vector<std::string> tags;
  std::uint64_t file_size{};
  std::int64_t modified_time_ns{};
  std::string fingerprint;
  MediaOnlineState state{MediaOnlineState::online};
  MediaProbeResult probe;
  ImageSequenceInfo sequence;
  std::string thumbnail_path;
  std::string waveform_path;
  std::string proxy_path;
  std::string optimized_media_path;
  std::string source_revision;
  std::uint64_t timeline_reference_count{};
};

struct MediaBin {
  std::string id;
  std::string name;
  std::string parent_id;
};

struct ImportJob {
  std::string id;
  std::string asset_id;
  ImportJobKind kind{ImportJobKind::probe};
  ImportJobState state{ImportJobState::queued};
  double progress{};
  std::string diagnostic;
};

struct ProfessionalImportCallbacks {
  std::function<bool(const std::string&)> exists;
  std::function<std::uint64_t(const std::string&)> file_size;
  std::function<std::int64_t(const std::string&)> modified_time_ns;
  std::function<std::string(const std::string&)> canonicalize;
  std::function<std::string(const std::string&, std::uint64_t, std::int64_t)> fingerprint;
  std::function<DigitorResult(const std::string&, MediaProbeResult&, std::string&)> probe;
  std::function<DigitorResult(const MediaAsset&, std::string&, std::string&)> generate_thumbnail;
  std::function<DigitorResult(const MediaAsset&, std::string&, std::string&)> generate_waveform;
  std::function<DigitorResult(const MediaAsset&, std::string&, std::string&)> generate_proxy;
  std::function<DigitorResult(const MediaAsset&, std::string&, std::string&)> generate_optimized_media;
  std::function<DigitorResult(const std::vector<MediaAsset>&, const std::vector<MediaBin>&,
                              std::string&)> persist;
};

struct ImportOptions {
  std::string bin_id{"root"};
  bool detect_image_sequence{true};
  bool create_thumbnail{true};
  bool create_waveform{true};
  bool create_proxy{};
  bool create_optimized_media{};
  int sequence_frame_rate_num{24};
  int sequence_frame_rate_den{1};
};

struct ImportResult {
  DigitorResult result{DIGITOR_RESULT_OK};
  std::string asset_id;
  bool duplicate{};
  std::string diagnostic;
};

struct ImportTelemetry {
  std::uint64_t imported_assets{};
  std::uint64_t duplicate_imports{};
  std::uint64_t offline_assets{};
  std::uint64_t relinked_assets{};
  std::uint64_t completed_jobs{};
  std::uint64_t failed_jobs{};
};

class ProfessionalImportEngine {
 public:
  explicit ProfessionalImportEngine(ProfessionalImportCallbacks callbacks);

  ImportResult import_path(const std::string& path, const ImportOptions& options = {});
  DigitorResult relink(const std::string& asset_id, const std::string& new_path,
                       std::string* diagnostic = nullptr);
  std::size_t relink_search(const std::vector<std::string>& candidates);
  void refresh_online_state();

  std::string create_bin(std::string name, std::string parent_id = "root");
  bool move_to_bin(const std::string& asset_id, const std::string& bin_id);
  bool add_tag(const std::string& asset_id, std::string tag);
  bool remove_tag(const std::string& asset_id, const std::string& tag);
  bool set_timeline_reference_count(const std::string& asset_id, std::uint64_t count);

  std::optional<MediaAsset> asset(const std::string& asset_id) const;
  std::vector<MediaAsset> assets() const;
  std::vector<MediaAsset> search(const std::string& query) const;
  std::vector<MediaBin> bins() const;
  std::vector<ImportJob> jobs() const;
  ImportTelemetry telemetry() const;

  DigitorResult persist(std::string* diagnostic = nullptr) const;

  static ImageSequenceInfo detect_sequence(const std::string& path,
                                           const std::vector<std::string>& sibling_paths,
                                           int frame_rate_num = 24,
                                           int frame_rate_den = 1);

 private:
  std::string make_id_locked(const char* prefix);
  DigitorResult run_job_locked(MediaAsset& asset, ImportJobKind kind,
                               const std::function<DigitorResult(const MediaAsset&, std::string&,
                                                                  std::string&)>& callback,
                               std::string MediaAsset::* output_member,
                               std::string& diagnostic);
  std::optional<std::string> duplicate_id_locked(const std::string& fingerprint) const;

  mutable std::mutex mutex_;
  ProfessionalImportCallbacks callbacks_;
  std::unordered_map<std::string, MediaAsset> assets_;
  std::unordered_map<std::string, MediaBin> bins_;
  std::vector<ImportJob> jobs_;
  ImportTelemetry telemetry_;
  std::uint64_t next_id_{1};
};

}  // namespace digitor
