#include "digitor/ffmpeg_export_runtime.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace digitor {
namespace {
std::string quote(const std::string& value) {
#ifdef _WIN32
  std::string out = "\"";
  for (const char c : value) out += c == '"' ? "\\\"" : std::string(1, c);
  return out + "\"";
#else
  std::string out = "'";
  for (const char c : value) out += c == '\'' ? "'\\''" : std::string(1, c);
  return out + "'";
#endif
}

int default_execute(const std::vector<std::string>& args) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
  // iOS does not allow spawning an external FFmpeg CLI process. Callers may
  // still inject a ProcessExecutor, while native/library and hardware export
  // paths remain available through their dedicated engine runtimes.
  (void)args;
  return -1;
#else
  std::ostringstream command;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i) command << ' ';
    command << quote(args[i]);
  }
  return std::system(command.str().c_str());
#endif
}

std::string codec_name(ExportCodec codec) {
  switch (codec) {
    case ExportCodec::h264: return "h264";
    case ExportCodec::hevc: return "hevc";
    case ExportCodec::av1: return "av1";
    case ExportCodec::prores: return "prores";
  }
  return "h264";
}
}  // namespace

std::string ffmpeg_video_encoder(EncoderBackend backend, ExportCodec codec) {
  if (codec == ExportCodec::prores) return "prores_ks";
  switch (backend) {
    case EncoderBackend::nvenc:
      return codec == ExportCodec::hevc ? "hevc_nvenc" : codec == ExportCodec::av1 ? "av1_nvenc" : "h264_nvenc";
    case EncoderBackend::quick_sync:
      return codec == ExportCodec::hevc ? "hevc_qsv" : codec == ExportCodec::av1 ? "av1_qsv" : "h264_qsv";
    case EncoderBackend::video_toolbox:
      return codec == ExportCodec::hevc ? "hevc_videotoolbox" : "h264_videotoolbox";
    case EncoderBackend::media_codec:
      return codec == ExportCodec::hevc ? "hevc_mediacodec" : "h264_mediacodec";
    case EncoderBackend::software:
      return codec == ExportCodec::hevc ? "libx265" : codec == ExportCodec::av1 ? "libaom-av1" : "libx264";
  }
  return "libx264";
}

FfmpegExportRuntime::FfmpegExportRuntime(FfmpegRuntimeConfig config, ProcessExecutor executor)
    : config_(std::move(config)), executor_(executor ? std::move(executor) : default_execute) {}

std::vector<std::string> FfmpegExportRuntime::build_arguments(const TranscodeRequest& request,
                                                               EncoderBackend backend) const {
  std::vector<std::string> args{config_.executable.string()};
  if (config_.hide_banner) args.emplace_back("-hide_banner");
  args.emplace_back("-nostdin");
  args.emplace_back(config_.overwrite ? "-y" : "-n");
  if (request.resume_from_us > 0) {
    args.emplace_back("-ss");
    args.emplace_back(std::to_string(static_cast<double>(request.resume_from_us) / 1'000'000.0));
  }
  args.emplace_back("-i");
  args.emplace_back(request.input_path.string());
  args.emplace_back("-map");
  args.emplace_back("0:v:0");
  args.emplace_back("-map");
  args.emplace_back("0:a?");
  args.emplace_back("-c:v");
  args.emplace_back(ffmpeg_video_encoder(backend, request.profile.codec));
  args.emplace_back("-b:v");
  args.emplace_back(std::to_string(request.profile.video_bitrate));
  args.emplace_back("-vf");
  args.emplace_back("scale=w=" + std::to_string(request.profile.width) + ":h=" +
                    std::to_string(request.profile.height) + ":force_original_aspect_ratio=decrease");
  args.emplace_back("-r");
  args.emplace_back(std::to_string(request.profile.fps_num) + "/" +
                    std::to_string(request.profile.fps_den));
  if (request.copy_audio) {
    args.emplace_back("-c:a"); args.emplace_back("copy");
  } else {
    args.emplace_back("-c:a"); args.emplace_back("aac");
    args.emplace_back("-ar"); args.emplace_back(std::to_string(request.profile.audio_sample_rate));
    args.emplace_back("-ac"); args.emplace_back(std::to_string(request.profile.audio_channels));
  }
  if (request.strip_metadata) {
    args.emplace_back("-map_metadata"); args.emplace_back("-1");
  }
  args.emplace_back("-movflags"); args.emplace_back("+faststart");
  args.emplace_back(request.output_path.string());
  return args;
}

TranscodeResult FfmpegExportRuntime::transcode(const TranscodeRequest& request,
                                                EncoderBackend backend,
                                                ResumableExportSession* session) {
  TranscodeResult result;
  result.output_path = request.output_path;
  if (request.require_zero_copy) {
    result.diagnostic = "file-based FFmpeg transcode is not zero-copy; submit ProcessedGpuFrame objects through ProductionHardwareEncodeSession";
    if (session) session->fail(result.diagnostic);
    return result;
  }
  if (cancelled_.exchange(false)) {
    result.cancelled = true;
    result.diagnostic = "cancelled before start";
    return result;
  }
  if (request.input_path.empty() || request.output_path.empty() ||
      request.profile.width <= 0 || request.profile.height <= 0) {
    result.diagnostic = "invalid transcode request";
    if (session) session->fail(result.diagnostic);
    return result;
  }
  std::error_code ec;
  std::filesystem::create_directories(request.output_path.parent_path(), ec);
  const auto temporary = request.output_path.string() + ".partial";
  auto staged = request;
  staged.output_path = temporary;
  if (session) session->start();
  result.exit_code = executor_(build_arguments(staged, backend));
  if (cancelled_.load()) {
    std::filesystem::remove(temporary, ec);
    result.cancelled = true;
    result.diagnostic = "cancelled";
    if (session) session->cancel();
    return result;
  }
  if (result.exit_code != 0 || !std::filesystem::exists(temporary)) {
    std::filesystem::remove(temporary, ec);
    result.diagnostic = "FFmpeg " + codec_name(request.profile.codec) + " transcode failed";
    if (session) session->fail(result.diagnostic);
    return result;
  }
  std::filesystem::remove(request.output_path, ec);
  std::filesystem::rename(temporary, request.output_path, ec);
  if (ec) {
    result.diagnostic = "failed to atomically publish output";
    if (session) session->fail(result.diagnostic);
    return result;
  }
  result.success = true;
  result.diagnostic = "completed";
  if (session) {
    session->advance(request.duration_us);
    session->complete();
  }
  return result;
}

TranscodeResult FfmpegExportRuntime::generate_proxy(const ProxyRequest& request,
                                                     std::uint64_t source_revision,
                                                     PersistentArtifactCache* cache,
                                                     ResumableExportSession* session) {
  TranscodeResult result;
  result.output_path = request.proxy_path;
  if (!validate_proxy_request(request)) {
    result.diagnostic = "invalid proxy request";
    return result;
  }
  const auto key = make_proxy_cache_key(request, source_revision);
  if (cache) {
    const auto cached = cache->load("proxy", key);
    if (cached) {
      std::error_code ec;
      std::filesystem::create_directories(std::filesystem::path(request.proxy_path).parent_path(), ec);
      std::ofstream out(request.proxy_path, std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(cached->data()), static_cast<std::streamsize>(cached->size()));
      result.success = static_cast<bool>(out);
      result.diagnostic = result.success ? "proxy cache hit" : "proxy cache restore failed";
      return result;
    }
  }
  TranscodeRequest transcode_request;
  transcode_request.input_path = request.source_path;
  transcode_request.output_path = request.proxy_path;
  transcode_request.kind = ExportJobKind::proxy_generation;
  transcode_request.profile.width = request.max_width;
  transcode_request.profile.height = request.max_height;
  transcode_request.profile.video_bitrate = request.bitrate;
  transcode_request.profile.prefer_hardware = false;
  transcode_request.copy_audio = request.preserve_audio;
  result = transcode(transcode_request, EncoderBackend::software, session);
  if (result.success && cache) {
    std::ifstream in(request.proxy_path, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    if (!bytes.empty()) cache->store("proxy", key, bytes);
  }
  return result;
}

void FfmpegExportRuntime::cancel() noexcept { cancelled_.store(true); }
bool FfmpegExportRuntime::cancelled() const noexcept { return cancelled_.load(); }

}  // namespace digitor