#include "digitor/professional_import.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace digitor {
namespace {
std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string basename(const std::string& path) {
  return std::filesystem::path(path).filename().string();
}

std::string revision_for(const MediaAsset& asset) {
  return asset.fingerprint + ":" + std::to_string(asset.file_size) + ":" +
         std::to_string(asset.modified_time_ns);
}
}  // namespace

ProfessionalImportEngine::ProfessionalImportEngine(ProfessionalImportCallbacks callbacks)
    : callbacks_(std::move(callbacks)) {
  bins_.emplace("root", MediaBin{"root", "Media Pool", {}});
}

std::string ProfessionalImportEngine::make_id_locked(const char* prefix) {
  return std::string(prefix) + "-" + std::to_string(next_id_++);
}

std::optional<std::string> ProfessionalImportEngine::duplicate_id_locked(
    const std::string& fingerprint) const {
  if (fingerprint.empty()) return std::nullopt;
  for (const auto& [id, asset] : assets_) {
    if (asset.fingerprint == fingerprint) return id;
  }
  return std::nullopt;
}

DigitorResult ProfessionalImportEngine::run_job_locked(
    MediaAsset& asset, ImportJobKind kind,
    const std::function<DigitorResult(const MediaAsset&, std::string&, std::string&)>& callback,
    std::string MediaAsset::* output_member, std::string& diagnostic) {
  if (!callback) return DIGITOR_RESULT_OK;
  ImportJob job{make_id_locked("job"), asset.id, kind, ImportJobState::running, 0.0, {}};
  jobs_.push_back(job);
  auto& live = jobs_.back();
  std::string output;
  std::string local;
  const auto result = callback(asset, output, local);
  if (result != DIGITOR_RESULT_OK || output.empty()) {
    live.state = ImportJobState::failed;
    live.diagnostic = local.empty() ? "import derivative generation failed" : local;
    ++telemetry_.failed_jobs;
    diagnostic = live.diagnostic;
    return result == DIGITOR_RESULT_OK ? DIGITOR_RESULT_BACKEND_UNAVAILABLE : result;
  }
  asset.*output_member = std::move(output);
  live.state = ImportJobState::completed;
  live.progress = 1.0;
  ++telemetry_.completed_jobs;
  return DIGITOR_RESULT_OK;
}

ImportResult ProfessionalImportEngine::import_path(const std::string& path,
                                                    const ImportOptions& options) {
  std::scoped_lock lock(mutex_);
  ImportResult out;
  if (path.empty() || !callbacks_.exists || !callbacks_.exists(path)) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "media path does not exist";
    return out;
  }
  if (!callbacks_.probe || !callbacks_.canonicalize || !callbacks_.file_size ||
      !callbacks_.modified_time_ns || !callbacks_.fingerprint) {
    out.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    out.diagnostic = "professional import callbacks are incomplete";
    return out;
  }

  MediaAsset asset;
  asset.id = make_id_locked("asset");
  asset.original_path = path;
  asset.canonical_path = callbacks_.canonicalize(path);
  asset.display_name = basename(path);
  asset.bin_id = bins_.contains(options.bin_id) ? options.bin_id : "root";
  asset.file_size = callbacks_.file_size(path);
  asset.modified_time_ns = callbacks_.modified_time_ns(path);
  asset.fingerprint = callbacks_.fingerprint(asset.canonical_path, asset.file_size,
                                              asset.modified_time_ns);
  asset.source_revision = revision_for(asset);

  if (const auto duplicate = duplicate_id_locked(asset.fingerprint)) {
    ++telemetry_.duplicate_imports;
    out.asset_id = *duplicate;
    out.duplicate = true;
    return out;
  }

  std::string diagnostic;
  if (callbacks_.probe(path, asset.probe, diagnostic) != DIGITOR_RESULT_OK) {
    out.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    out.diagnostic = diagnostic.empty() ? "media probe failed" : diagnostic;
    return out;
  }

  if (options.detect_image_sequence && asset.probe.kind == MediaAssetKind::still_image) {
    asset.sequence.frame_rate_num = options.sequence_frame_rate_num;
    asset.sequence.frame_rate_den = options.sequence_frame_rate_den;
  }

  assets_.emplace(asset.id, asset);
  auto& stored = assets_.at(asset.id);
  auto rollback = [&] { assets_.erase(asset.id); };

  if (options.create_thumbnail && callbacks_.generate_thumbnail) {
    const auto result = run_job_locked(stored, ImportJobKind::thumbnail,
                                       callbacks_.generate_thumbnail,
                                       &MediaAsset::thumbnail_path, diagnostic);
    if (result != DIGITOR_RESULT_OK) {
      rollback();
      out.result = result;
      out.diagnostic = diagnostic;
      return out;
    }
  }
  if (options.create_waveform && callbacks_.generate_waveform &&
      (stored.probe.kind == MediaAssetKind::audio || stored.probe.kind == MediaAssetKind::video)) {
    const auto result = run_job_locked(stored, ImportJobKind::waveform,
                                       callbacks_.generate_waveform,
                                       &MediaAsset::waveform_path, diagnostic);
    if (result != DIGITOR_RESULT_OK) {
      rollback();
      out.result = result;
      out.diagnostic = diagnostic;
      return out;
    }
  }
  if (options.create_proxy && callbacks_.generate_proxy) {
    const auto result = run_job_locked(stored, ImportJobKind::proxy, callbacks_.generate_proxy,
                                       &MediaAsset::proxy_path, diagnostic);
    if (result != DIGITOR_RESULT_OK) {
      rollback();
      out.result = result;
      out.diagnostic = diagnostic;
      return out;
    }
  }
  if (options.create_optimized_media && callbacks_.generate_optimized_media) {
    const auto result = run_job_locked(stored, ImportJobKind::optimized_media,
                                       callbacks_.generate_optimized_media,
                                       &MediaAsset::optimized_media_path, diagnostic);
    if (result != DIGITOR_RESULT_OK) {
      rollback();
      out.result = result;
      out.diagnostic = diagnostic;
      return out;
    }
  }

  ++telemetry_.imported_assets;
  out.asset_id = stored.id;
  return out;
}

DigitorResult ProfessionalImportEngine::relink(const std::string& asset_id,
                                               const std::string& new_path,
                                               std::string* diagnostic) {
  std::scoped_lock lock(mutex_);
  const auto it = assets_.find(asset_id);
  if (it == assets_.end() || !callbacks_.exists || !callbacks_.exists(new_path)) {
    if (diagnostic) *diagnostic = "asset or replacement media is unavailable";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  auto& asset = it->second;
  const auto size = callbacks_.file_size(new_path);
  const auto modified = callbacks_.modified_time_ns(new_path);
  const auto canonical = callbacks_.canonicalize(new_path);
  const auto fingerprint = callbacks_.fingerprint(canonical, size, modified);
  if (!asset.fingerprint.empty() && fingerprint != asset.fingerprint) {
    if (diagnostic) *diagnostic = "replacement media fingerprint does not match asset";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  asset.original_path = new_path;
  asset.canonical_path = canonical;
  asset.file_size = size;
  asset.modified_time_ns = modified;
  asset.fingerprint = fingerprint;
  asset.source_revision = revision_for(asset);
  asset.state = MediaOnlineState::online;
  ++telemetry_.relinked_assets;
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

std::size_t ProfessionalImportEngine::relink_search(const std::vector<std::string>& candidates) {
  std::vector<std::pair<std::string, std::string>> matches;
  {
    std::scoped_lock lock(mutex_);
    for (const auto& [id, asset] : assets_) {
      if (asset.state == MediaOnlineState::online) continue;
      for (const auto& path : candidates) {
        if (!callbacks_.exists || !callbacks_.exists(path)) continue;
        const auto canonical = callbacks_.canonicalize(path);
        const auto size = callbacks_.file_size(path);
        const auto modified = callbacks_.modified_time_ns(path);
        if (callbacks_.fingerprint(canonical, size, modified) == asset.fingerprint) {
          matches.emplace_back(id, path);
          break;
        }
      }
    }
  }
  std::size_t count{};
  for (const auto& [id, path] : matches) {
    if (relink(id, path) == DIGITOR_RESULT_OK) ++count;
  }
  return count;
}

void ProfessionalImportEngine::refresh_online_state() {
  std::scoped_lock lock(mutex_);
  telemetry_.offline_assets = 0;
  for (auto& [id, asset] : assets_) {
    if (!callbacks_.exists || !callbacks_.exists(asset.original_path)) {
      asset.state = MediaOnlineState::offline;
      ++telemetry_.offline_assets;
      continue;
    }
    const auto size = callbacks_.file_size(asset.original_path);
    const auto modified = callbacks_.modified_time_ns(asset.original_path);
    asset.state = (size == asset.file_size && modified == asset.modified_time_ns)
                      ? MediaOnlineState::online
                      : MediaOnlineState::changed;
  }
}

std::string ProfessionalImportEngine::create_bin(std::string name, std::string parent_id) {
  std::scoped_lock lock(mutex_);
  if (!bins_.contains(parent_id)) parent_id = "root";
  const auto id = make_id_locked("bin");
  bins_.emplace(id, MediaBin{id, std::move(name), std::move(parent_id)});
  return id;
}

bool ProfessionalImportEngine::move_to_bin(const std::string& asset_id,
                                           const std::string& bin_id) {
  std::scoped_lock lock(mutex_);
  const auto asset = assets_.find(asset_id);
  if (asset == assets_.end() || !bins_.contains(bin_id)) return false;
  asset->second.bin_id = bin_id;
  return true;
}

bool ProfessionalImportEngine::add_tag(const std::string& asset_id, std::string tag) {
  std::scoped_lock lock(mutex_);
  const auto it = assets_.find(asset_id);
  if (it == assets_.end() || tag.empty()) return false;
  if (std::find(it->second.tags.begin(), it->second.tags.end(), tag) == it->second.tags.end())
    it->second.tags.push_back(std::move(tag));
  return true;
}

bool ProfessionalImportEngine::remove_tag(const std::string& asset_id,
                                          const std::string& tag) {
  std::scoped_lock lock(mutex_);
  const auto it = assets_.find(asset_id);
  if (it == assets_.end()) return false;
  const auto before = it->second.tags.size();
  std::erase(it->second.tags, tag);
  return before != it->second.tags.size();
}

bool ProfessionalImportEngine::set_timeline_reference_count(const std::string& asset_id,
                                                            std::uint64_t count) {
  std::scoped_lock lock(mutex_);
  const auto it = assets_.find(asset_id);
  if (it == assets_.end()) return false;
  it->second.timeline_reference_count = count;
  return true;
}

std::optional<MediaAsset> ProfessionalImportEngine::asset(const std::string& asset_id) const {
  std::scoped_lock lock(mutex_);
  const auto it = assets_.find(asset_id);
  return it == assets_.end() ? std::nullopt : std::optional<MediaAsset>(it->second);
}

std::vector<MediaAsset> ProfessionalImportEngine::assets() const {
  std::scoped_lock lock(mutex_);
  std::vector<MediaAsset> out;
  out.reserve(assets_.size());
  for (const auto& [id, asset] : assets_) out.push_back(asset);
  return out;
}

std::vector<MediaAsset> ProfessionalImportEngine::search(const std::string& query) const {
  const auto needle = lower(query);
  std::scoped_lock lock(mutex_);
  std::vector<MediaAsset> out;
  for (const auto& [id, asset] : assets_) {
    bool match = lower(asset.display_name).find(needle) != std::string::npos ||
                 lower(asset.original_path).find(needle) != std::string::npos;
    for (const auto& tag : asset.tags)
      match = match || lower(tag).find(needle) != std::string::npos;
    if (match) out.push_back(asset);
  }
  return out;
}

std::vector<MediaBin> ProfessionalImportEngine::bins() const {
  std::scoped_lock lock(mutex_);
  std::vector<MediaBin> out;
  out.reserve(bins_.size());
  for (const auto& [id, bin] : bins_) out.push_back(bin);
  return out;
}

std::vector<ImportJob> ProfessionalImportEngine::jobs() const {
  std::scoped_lock lock(mutex_);
  return jobs_;
}

ImportTelemetry ProfessionalImportEngine::telemetry() const {
  std::scoped_lock lock(mutex_);
  return telemetry_;
}

DigitorResult ProfessionalImportEngine::persist(std::string* diagnostic) const {
  std::vector<MediaAsset> asset_copy;
  std::vector<MediaBin> bin_copy;
  {
    std::scoped_lock lock(mutex_);
    if (!callbacks_.persist) {
      if (diagnostic) *diagnostic = "media-pool persistence callback is unavailable";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    for (const auto& [id, asset] : assets_) asset_copy.push_back(asset);
    for (const auto& [id, bin] : bins_) bin_copy.push_back(bin);
  }
  std::string local;
  const auto result = callbacks_.persist(asset_copy, bin_copy, local);
  if (diagnostic) *diagnostic = local;
  return result;
}

ImageSequenceInfo ProfessionalImportEngine::detect_sequence(
    const std::string& path, const std::vector<std::string>& sibling_paths, int frame_rate_num,
    int frame_rate_den) {
  ImageSequenceInfo out;
  out.frame_rate_num = frame_rate_num;
  out.frame_rate_den = frame_rate_den;
  const auto file = std::filesystem::path(path).filename().string();
  const auto dot = file.find_last_of('.');
  if (dot == std::string::npos) return out;
  const auto stem = file.substr(0, dot);
  std::size_t digit_start = stem.size();
  while (digit_start > 0 && std::isdigit(static_cast<unsigned char>(stem[digit_start - 1])))
    --digit_start;
  if (digit_start == stem.size()) return out;
  out.prefix = stem.substr(0, digit_start);
  out.extension = file.substr(dot);
  out.padding = static_cast<int>(stem.size() - digit_start);

  std::vector<int> frames;
  for (const auto& sibling : sibling_paths) {
    const auto candidate = std::filesystem::path(sibling).filename().string();
    const auto candidate_dot = candidate.find_last_of('.');
    if (candidate_dot == std::string::npos || candidate.substr(candidate_dot) != out.extension)
      continue;
    const auto candidate_stem = candidate.substr(0, candidate_dot);
    if (!candidate_stem.starts_with(out.prefix) ||
        candidate_stem.size() != out.prefix.size() + static_cast<std::size_t>(out.padding))
      continue;
    const auto digits = candidate_stem.substr(out.prefix.size());
    if (!std::all_of(digits.begin(), digits.end(),
                     [](unsigned char c) { return std::isdigit(c); }))
      continue;
    frames.push_back(std::stoi(digits));
  }
  if (frames.size() < 2) return out;
  std::sort(frames.begin(), frames.end());
  frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
  out.detected = true;
  out.first_frame = frames.front();
  out.last_frame = frames.back();
  for (int frame = out.first_frame; frame <= out.last_frame; ++frame)
    if (!std::binary_search(frames.begin(), frames.end(), frame)) out.missing_frames.push_back(frame);
  return out;
}

}  // namespace digitor
