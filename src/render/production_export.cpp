#include "digitor/production_export.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace digitor {
namespace {

bool supports(const EncoderCapability& capability, const ExportProfile& profile) noexcept {
  return capability.available && capability.max_width >= profile.width &&
         capability.max_height >= profile.height && (!profile.ten_bit || capability.ten_bit) &&
         std::find(capability.codecs.begin(), capability.codecs.end(), profile.codec) !=
             capability.codecs.end();
}

int backend_rank(EncoderBackend backend) noexcept {
  switch (backend) {
    case EncoderBackend::nvenc: return 0;
    case EncoderBackend::quick_sync: return 1;
    case EncoderBackend::video_toolbox: return 2;
    case EncoderBackend::media_codec: return 3;
    case EncoderBackend::software: return 4;
  }
  return 5;
}

std::string safe_component(const std::string& input) {
  std::ostringstream out;
  for (const unsigned char ch : input) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
      out << static_cast<char>(ch);
    } else {
      out << '_' << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return out.str();
}

int to_int(EncoderBackend value) noexcept { return static_cast<int>(value); }
int to_int(ExportJobKind value) noexcept { return static_cast<int>(value); }
int to_int(ExportState value) noexcept { return static_cast<int>(value); }

template <typename Enum>
bool in_enum_range(int value, int max_value, Enum& out) noexcept {
  if (value < 0 || value > max_value) return false;
  out = static_cast<Enum>(value);
  return true;
}

}  // namespace

ExportSelection EncoderSelector::select(
    const ExportProfile& profile,
    const std::vector<EncoderCapability>& capabilities) noexcept {
  if (profile.width <= 0 || profile.height <= 0 || profile.fps_num <= 0 ||
      profile.fps_den <= 0 || profile.video_bitrate <= 0 ||
      profile.audio_sample_rate <= 0 || profile.audio_channels <= 0) {
    return {false, EncoderBackend::software, false, "invalid export profile"};
  }

  std::vector<const EncoderCapability*> candidates;
  for (const auto& capability : capabilities) {
    if (supports(capability, profile)) candidates.push_back(&capability);
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const auto* a, const auto* b) {
    return backend_rank(a->backend) < backend_rank(b->backend);
  });

  if (profile.prefer_hardware) {
    const auto it = std::find_if(candidates.begin(), candidates.end(),
                                 [](const auto* item) { return item->hardware; });
    if (it != candidates.end()) {
      return {true, (*it)->backend, false, "hardware encoder selected"};
    }
  }

  const auto software = std::find_if(candidates.begin(), candidates.end(),
                                     [](const auto* item) {
                                       return item->backend == EncoderBackend::software;
                                     });
  if (software != candidates.end() && profile.allow_software_fallback) {
    return {true, EncoderBackend::software, profile.prefer_hardware,
            profile.prefer_hardware ? "software fallback selected" : "software encoder selected"};
  }

  if (!profile.prefer_hardware && !candidates.empty()) {
    return {true, candidates.front()->backend, false, "available encoder selected"};
  }
  return {false, EncoderBackend::software, false, "no compatible encoder available"};
}

PersistentArtifactCache::PersistentArtifactCache(std::filesystem::path root,
                                                 std::uint64_t max_bytes)
    : root_(std::move(root)), max_bytes_(max_bytes) {
  std::error_code error;
  std::filesystem::create_directories(root_, error);
}

std::filesystem::path PersistentArtifactCache::path_for(const std::string& namespace_id,
                                                        const std::string& key) const {
  return root_ / safe_component(namespace_id) / (safe_component(key) + ".bin");
}

bool PersistentArtifactCache::contains(const std::string& namespace_id,
                                       const std::string& key) const {
  std::error_code error;
  return std::filesystem::is_regular_file(path_for(namespace_id, key), error);
}

bool PersistentArtifactCache::store(const std::string& namespace_id, const std::string& key,
                                    const std::vector<std::uint8_t>& bytes) {
  if (namespace_id.empty() || key.empty() || bytes.size() > max_bytes_) return false;
  const auto path = path_for(namespace_id, key);
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) return false;
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!stream) return false;
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
  }
  if (error) return false;
  prune();
  return contains(namespace_id, key);
}

std::optional<std::vector<std::uint8_t>> PersistentArtifactCache::load(
    const std::string& namespace_id, const std::string& key) {
  const auto path = path_for(namespace_id, key);
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  stream.seekg(0, std::ios::end);
  const auto size = stream.tellg();
  if (size < 0) return std::nullopt;
  stream.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!stream) return std::nullopt;
  std::error_code error;
  std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), error);
  return bytes;
}

bool PersistentArtifactCache::erase_namespace(const std::string& namespace_id) {
  if (namespace_id.empty()) return false;
  std::error_code error;
  std::filesystem::remove_all(root_ / safe_component(namespace_id), error);
  return !error;
}

std::uint64_t PersistentArtifactCache::size_bytes() const {
  std::uint64_t total = 0;
  std::error_code error;
  if (!std::filesystem::exists(root_, error)) return 0;
  for (std::filesystem::recursive_directory_iterator it(root_, error), end; it != end && !error;
       it.increment(error)) {
    if (it->is_regular_file(error)) total += it->file_size(error);
  }
  return total;
}

void PersistentArtifactCache::prune() {
  struct Entry { std::filesystem::path path; std::filesystem::file_time_type time; std::uint64_t size; };
  std::vector<Entry> entries;
  std::uint64_t total = 0;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator it(root_, error), end; it != end && !error;
       it.increment(error)) {
    if (!it->is_regular_file(error) || it->path().extension() == ".tmp") continue;
    const auto size = static_cast<std::uint64_t>(it->file_size(error));
    entries.push_back({it->path(), it->last_write_time(error), size});
    total += size;
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    return a.time < b.time;
  });
  for (const auto& entry : entries) {
    if (total <= max_bytes_) break;
    std::filesystem::remove(entry.path, error);
    if (!error) total -= std::min(total, entry.size);
    error.clear();
  }
}

ResumableExportSession::ResumableExportSession(ExportCheckpoint checkpoint,
                                               std::filesystem::path checkpoint_path)
    : checkpoint_(std::move(checkpoint)), checkpoint_path_(std::move(checkpoint_path)) {
  checkpoint_.duration_us = std::max<std::int64_t>(0, checkpoint_.duration_us);
  checkpoint_.completed_us = std::clamp<std::int64_t>(checkpoint_.completed_us, 0,
                                                       checkpoint_.duration_us);
}

bool ResumableExportSession::start() {
  if (checkpoint_.project_id.empty() || checkpoint_.output_path.empty() ||
      checkpoint_.duration_us <= 0 || checkpoint_.state == ExportState::completed ||
      checkpoint_.state == ExportState::cancelled) return false;
  checkpoint_.state = ExportState::running;
  ++generation_;
  diagnostic_.clear();
  return persist();
}

bool ResumableExportSession::pause() {
  if (checkpoint_.state != ExportState::running) return false;
  checkpoint_.state = ExportState::paused;
  return persist();
}

bool ResumableExportSession::resume() {
  if (checkpoint_.state != ExportState::paused && checkpoint_.state != ExportState::failed) return false;
  checkpoint_.state = ExportState::running;
  ++generation_;
  diagnostic_.clear();
  return persist();
}

bool ResumableExportSession::cancel() {
  if (checkpoint_.state == ExportState::completed || checkpoint_.state == ExportState::cancelled) return false;
  checkpoint_.state = ExportState::cancelled;
  ++generation_;
  return persist();
}

bool ResumableExportSession::fail(std::string diagnostic) {
  if (checkpoint_.state != ExportState::running) return false;
  checkpoint_.state = ExportState::failed;
  diagnostic_ = std::move(diagnostic);
  return persist();
}

bool ResumableExportSession::advance(std::int64_t completed_us) {
  if (checkpoint_.state != ExportState::running) return false;
  checkpoint_.completed_us = std::clamp<std::int64_t>(completed_us, checkpoint_.completed_us,
                                                       checkpoint_.duration_us);
  if (checkpoint_.completed_us == checkpoint_.duration_us) checkpoint_.state = ExportState::completed;
  return persist();
}

bool ResumableExportSession::complete() {
  if (checkpoint_.state != ExportState::running && checkpoint_.state != ExportState::paused) return false;
  checkpoint_.completed_us = checkpoint_.duration_us;
  checkpoint_.state = ExportState::completed;
  return persist();
}

bool ResumableExportSession::persist() const {
  if (checkpoint_path_.empty()) return false;
  std::error_code error;
  std::filesystem::create_directories(checkpoint_path_.parent_path(), error);
  if (error) return false;
  const auto temporary = checkpoint_path_.string() + ".tmp";
  {
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream) return false;
    stream << std::quoted(checkpoint_.project_id) << '\n'
           << std::quoted(checkpoint_.output_path) << '\n'
           << to_int(checkpoint_.kind) << ' ' << to_int(checkpoint_.state) << ' '
           << checkpoint_.duration_us << ' ' << checkpoint_.completed_us << ' '
           << checkpoint_.timeline_revision << ' ' << checkpoint_.render_revision << ' '
           << to_int(checkpoint_.backend) << '\n';
    if (!stream) return false;
  }
  std::filesystem::rename(temporary, checkpoint_path_, error);
  if (error) {
    std::filesystem::remove(checkpoint_path_, error);
    error.clear();
    std::filesystem::rename(temporary, checkpoint_path_, error);
  }
  return !error;
}

ExportSnapshot ResumableExportSession::snapshot() const {
  const double progress = checkpoint_.duration_us > 0
                              ? static_cast<double>(checkpoint_.completed_us) /
                                    static_cast<double>(checkpoint_.duration_us)
                              : 0.0;
  return {checkpoint_.state, checkpoint_.duration_us, checkpoint_.completed_us,
          std::clamp(progress, 0.0, 1.0), generation_, diagnostic_};
}

const ExportCheckpoint& ResumableExportSession::checkpoint() const noexcept { return checkpoint_; }

std::optional<ExportCheckpoint> ResumableExportSession::load_checkpoint(
    const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream) return std::nullopt;
  ExportCheckpoint checkpoint;
  int kind = 0;
  int state = 0;
  int backend = 0;
  if (!(stream >> std::quoted(checkpoint.project_id) >> std::quoted(checkpoint.output_path) >>
        kind >> state >> checkpoint.duration_us >> checkpoint.completed_us >>
        checkpoint.timeline_revision >> checkpoint.render_revision >> backend)) {
    return std::nullopt;
  }
  if (!in_enum_range(kind, to_int(ExportJobKind::cache_warmup), checkpoint.kind) ||
      !in_enum_range(state, to_int(ExportState::failed), checkpoint.state) ||
      !in_enum_range(backend, to_int(EncoderBackend::media_codec), checkpoint.backend) ||
      checkpoint.project_id.empty() || checkpoint.output_path.empty() ||
      checkpoint.duration_us <= 0 || checkpoint.completed_us < 0 ||
      checkpoint.completed_us > checkpoint.duration_us) {
    return std::nullopt;
  }
  return checkpoint;
}

bool validate_proxy_request(const ProxyRequest& request) noexcept {
  return !request.clip_id.empty() && !request.source_path.empty() && !request.proxy_path.empty() &&
         request.max_width > 0 && request.max_height > 0 && request.bitrate > 0;
}

std::string make_proxy_cache_key(const ProxyRequest& request, std::uint64_t source_revision) {
  std::ostringstream stream;
  stream << safe_component(request.clip_id) << '-' << source_revision << '-' << request.max_width
         << 'x' << request.max_height << '-' << request.bitrate << '-'
         << (request.preserve_audio ? 'a' : 'v');
  return stream.str();
}

}  // namespace digitor
