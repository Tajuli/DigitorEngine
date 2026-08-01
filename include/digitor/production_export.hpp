#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace digitor {

enum class EncoderBackend { software, nvenc, quick_sync, video_toolbox, media_codec };
enum class ExportCodec { h264, hevc, av1, prores };
enum class ExportState { idle, running, paused, completed, cancelled, failed };
enum class ExportJobKind { final_export, proxy_generation, cache_warmup };

struct EncoderCapability {
  EncoderBackend backend{EncoderBackend::software};
  std::vector<ExportCodec> codecs;
  std::int32_t max_width{};
  std::int32_t max_height{};
  bool ten_bit{};
  bool hardware{};
  bool available{true};
};

struct ExportProfile {
  ExportCodec codec{ExportCodec::h264};
  std::int32_t width{1920};
  std::int32_t height{1080};
  std::int32_t fps_num{30};
  std::int32_t fps_den{1};
  std::int64_t video_bitrate{12'000'000};
  std::int32_t audio_sample_rate{48'000};
  std::int32_t audio_channels{2};
  bool ten_bit{};
  bool prefer_hardware{true};
  bool allow_software_fallback{true};
};

struct ExportSelection {
  bool supported{};
  EncoderBackend backend{EncoderBackend::software};
  bool used_fallback{};
  std::string diagnostic;
};

struct ExportCheckpoint {
  std::string project_id;
  std::string output_path;
  ExportJobKind kind{ExportJobKind::final_export};
  ExportState state{ExportState::idle};
  std::int64_t duration_us{};
  std::int64_t completed_us{};
  std::uint64_t timeline_revision{};
  std::uint64_t render_revision{};
  EncoderBackend backend{EncoderBackend::software};
};

struct ExportSnapshot {
  ExportState state{ExportState::idle};
  std::int64_t duration_us{};
  std::int64_t completed_us{};
  double progress{};
  std::uint64_t generation{};
  std::string diagnostic;
};

struct ProxyRequest {
  std::string clip_id;
  std::string source_path;
  std::string proxy_path;
  std::int32_t max_width{1280};
  std::int32_t max_height{720};
  std::int64_t bitrate{3'000'000};
  bool preserve_audio{true};
};

class EncoderSelector final {
 public:
  [[nodiscard]] static ExportSelection select(
      const ExportProfile& profile,
      const std::vector<EncoderCapability>& capabilities) noexcept;
};

class PersistentArtifactCache final {
 public:
  explicit PersistentArtifactCache(std::filesystem::path root,
                                   std::uint64_t max_bytes = 4ULL * 1024ULL * 1024ULL * 1024ULL);
  [[nodiscard]] std::filesystem::path path_for(const std::string& namespace_id,
                                               const std::string& key) const;
  [[nodiscard]] bool contains(const std::string& namespace_id,
                              const std::string& key) const;
  bool store(const std::string& namespace_id, const std::string& key,
             const std::vector<std::uint8_t>& bytes);
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> load(
      const std::string& namespace_id, const std::string& key);
  bool erase_namespace(const std::string& namespace_id);
  void prune();
  [[nodiscard]] std::uint64_t size_bytes() const;

 private:
  std::filesystem::path root_;
  std::uint64_t max_bytes_{};
};

class ResumableExportSession final {
 public:
  ResumableExportSession(ExportCheckpoint checkpoint,
                         std::filesystem::path checkpoint_path);
  bool start();
  bool pause();
  bool resume();
  bool cancel();
  bool fail(std::string diagnostic);
  bool advance(std::int64_t completed_us);
  bool complete();
  [[nodiscard]] bool persist() const;
  [[nodiscard]] ExportSnapshot snapshot() const;
  [[nodiscard]] const ExportCheckpoint& checkpoint() const noexcept;
  [[nodiscard]] static std::optional<ExportCheckpoint> load_checkpoint(
      const std::filesystem::path& path);

 private:
  ExportCheckpoint checkpoint_;
  std::filesystem::path checkpoint_path_;
  std::uint64_t generation_{};
  std::string diagnostic_;
};

[[nodiscard]] bool validate_proxy_request(const ProxyRequest& request) noexcept;
[[nodiscard]] std::string make_proxy_cache_key(const ProxyRequest& request,
                                               std::uint64_t source_revision);

}  // namespace digitor
