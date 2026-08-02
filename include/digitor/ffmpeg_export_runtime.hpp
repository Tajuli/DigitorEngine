#pragma once

#include "digitor/production_export.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace digitor {

struct FfmpegRuntimeConfig {
  std::filesystem::path executable{"ffmpeg"};
  bool overwrite{true};
  bool hide_banner{true};
};

struct TranscodeRequest {
  std::filesystem::path input_path;
  std::filesystem::path output_path;
  ExportProfile profile;
  ExportJobKind kind{ExportJobKind::final_export};
  std::int64_t duration_us{};
  std::int64_t resume_from_us{};
  bool copy_audio{false};
  bool strip_metadata{true};
  // File-based FFmpeg transcode necessarily re-opens and re-decodes the source.
  // Strict zero-copy exports must use ProductionHardwareEncodeSession with
  // ProcessedGpuFrame submission instead of this runtime.
  bool require_zero_copy{false};
};

struct TranscodeResult {
  bool success{};
  bool cancelled{};
  int exit_code{-1};
  std::filesystem::path output_path;
  std::string diagnostic;
};

using ProcessExecutor = std::function<int(const std::vector<std::string>&)>;

class FfmpegExportRuntime final {
 public:
  explicit FfmpegExportRuntime(FfmpegRuntimeConfig config = {},
                               ProcessExecutor executor = {});

  [[nodiscard]] std::vector<std::string> build_arguments(
      const TranscodeRequest& request,
      EncoderBackend backend) const;

  [[nodiscard]] TranscodeResult transcode(
      const TranscodeRequest& request,
      EncoderBackend backend,
      ResumableExportSession* session = nullptr);

  [[nodiscard]] TranscodeResult generate_proxy(
      const ProxyRequest& request,
      std::uint64_t source_revision,
      PersistentArtifactCache* cache = nullptr,
      ResumableExportSession* session = nullptr);

  void cancel() noexcept;
  [[nodiscard]] bool cancelled() const noexcept;

 private:
  FfmpegRuntimeConfig config_;
  ProcessExecutor executor_;
  std::atomic_bool cancelled_{false};
};

[[nodiscard]] std::string ffmpeg_video_encoder(EncoderBackend backend,
                                                ExportCodec codec);

}  // namespace digitor
