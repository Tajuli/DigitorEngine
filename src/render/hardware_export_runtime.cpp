#include "digitor/hardware_export_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>
#include <unordered_set>

namespace digitor {
namespace {

int default_text_executor(const std::vector<std::string>& args, std::string& output) {
  if (args.empty()) return -1;
  std::ostringstream command;
  for (const auto& arg : args) command << '"' << arg << "\" ";
#if defined(_WIN32)
  FILE* pipe = _popen(command.str().c_str(), "r");
#else
  FILE* pipe = popen(command.str().c_str(), "r");
#endif
  if (!pipe) return -1;
  std::array<char, 4096> buffer{};
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) output += buffer.data();
#if defined(_WIN32)
  return _pclose(pipe);
#else
  return pclose(pipe);
#endif
}

bool has_encoder(const RuntimeEncoderInventory& inventory, const char* name) noexcept {
  return std::find(inventory.encoder_names.begin(), inventory.encoder_names.end(), name) !=
         inventory.encoder_names.end();
}

void add_capability(std::vector<EncoderCapability>& out, EncoderBackend backend,
                    std::vector<ExportCodec> codecs, bool ten_bit) {
  if (codecs.empty()) return;
  out.push_back({backend, std::move(codecs), 8192, 8192, ten_bit,
                 backend != EncoderBackend::software, true});
}

std::vector<ExportCodec> nvenc_codecs(const RuntimeEncoderInventory& inventory) {
  std::vector<ExportCodec> codecs;
  if (has_encoder(inventory, "h264_nvenc")) codecs.push_back(ExportCodec::h264);
  if (has_encoder(inventory, "hevc_nvenc")) codecs.push_back(ExportCodec::hevc);
  if (has_encoder(inventory, "av1_nvenc")) codecs.push_back(ExportCodec::av1);
  return codecs;
}

std::vector<ExportCodec> qsv_codecs(const RuntimeEncoderInventory& inventory) {
  std::vector<ExportCodec> codecs;
  if (has_encoder(inventory, "h264_qsv")) codecs.push_back(ExportCodec::h264);
  if (has_encoder(inventory, "hevc_qsv")) codecs.push_back(ExportCodec::hevc);
  if (has_encoder(inventory, "av1_qsv")) codecs.push_back(ExportCodec::av1);
  return codecs;
}

std::vector<ExportCodec> videotoolbox_codecs(const RuntimeEncoderInventory& inventory) {
  std::vector<ExportCodec> codecs;
  if (has_encoder(inventory, "h264_videotoolbox")) codecs.push_back(ExportCodec::h264);
  if (has_encoder(inventory, "hevc_videotoolbox")) codecs.push_back(ExportCodec::hevc);
  return codecs;
}

std::vector<ExportCodec> mediacodec_codecs(const RuntimeEncoderInventory& inventory) {
  std::vector<ExportCodec> codecs;
  if (has_encoder(inventory, "h264_mediacodec")) codecs.push_back(ExportCodec::h264);
  if (has_encoder(inventory, "hevc_mediacodec")) codecs.push_back(ExportCodec::hevc);
  return codecs;
}

std::vector<ExportCodec> software_codecs(const RuntimeEncoderInventory& inventory) {
  std::vector<ExportCodec> codecs;
  if (has_encoder(inventory, "libx264")) codecs.push_back(ExportCodec::h264);
  if (has_encoder(inventory, "libx265")) codecs.push_back(ExportCodec::hevc);
  if (has_encoder(inventory, "libaom-av1")) codecs.push_back(ExportCodec::av1);
  if (has_encoder(inventory, "prores_ks")) codecs.push_back(ExportCodec::prores);
  return codecs;
}

}  // namespace

RuntimeEncoderInventory probe_ffmpeg_encoder_inventory(const std::string& binary,
                                                        TextCommandExecutor executor) {
  RuntimeEncoderInventory inventory;
  std::string output;
  const auto run = executor ? std::move(executor) : TextCommandExecutor(default_text_executor);
  const int exit_code = run({binary, "-hide_banner", "-encoders"}, output);
  if (exit_code != 0) {
    inventory.diagnostic = "ffmpeg encoder inventory probe failed";
    return inventory;
  }
  inventory.ffmpeg_available = true;
  std::istringstream lines(output);
  std::string line;
  std::unordered_set<std::string> unique;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string flags;
    std::string name;
    fields >> flags >> name;
    if (flags.size() >= 6 && flags.front() == 'V' && !name.empty() && unique.insert(name).second) {
      inventory.encoder_names.push_back(std::move(name));
    }
  }
  std::sort(inventory.encoder_names.begin(), inventory.encoder_names.end());
  inventory.diagnostic = inventory.encoder_names.empty() ? "no video encoders reported" : "ok";
  return inventory;
}

std::vector<EncoderCapability> capabilities_from_inventory(
    const RuntimeEncoderInventory& inventory) noexcept {
  std::vector<EncoderCapability> result;
  if (!inventory.ffmpeg_available) return result;
  add_capability(result, EncoderBackend::nvenc, nvenc_codecs(inventory), true);
  add_capability(result, EncoderBackend::quick_sync, qsv_codecs(inventory), true);
  add_capability(result, EncoderBackend::video_toolbox, videotoolbox_codecs(inventory), true);
  add_capability(result, EncoderBackend::media_codec, mediacodec_codecs(inventory), true);
  add_capability(result, EncoderBackend::software, software_codecs(inventory), true);
  return result;
}

HardwareAwareExportRuntime::HardwareAwareExportRuntime(std::string binary,
                                                       TextCommandExecutor probe_executor,
                                                       ProcessExecutor process_executor)
    : ffmpeg_binary_(std::move(binary)),
      probe_executor_(std::move(probe_executor)),
      process_executor_(std::move(process_executor)) {}

RuntimeEncoderInventory HardwareAwareExportRuntime::probe() const {
  return probe_ffmpeg_encoder_inventory(ffmpeg_binary_, probe_executor_);
}

HardwareExportResult HardwareAwareExportRuntime::execute(
    const TranscodeRequest& request,
    const std::vector<EncoderCapability>& extra_capabilities) const {
  auto capabilities = capabilities_from_inventory(probe());
  capabilities.insert(capabilities.end(), extra_capabilities.begin(), extra_capabilities.end());
  const auto selection = EncoderSelector::select(request.profile, capabilities);
  HardwareExportResult result;
  result.requested_backend = selection.backend;
  result.executed_backend = selection.backend;
  result.used_fallback = selection.used_fallback;
  result.diagnostic = selection.diagnostic;
  if (!selection.supported) return result;

  FfmpegRuntimeConfig config;
  config.executable = ffmpeg_binary_;
  FfmpegExportRuntime runtime(config, process_executor_);
  auto transcode = runtime.transcode(request, selection.backend);
  result.success = transcode.success;
  result.process_exit_code = transcode.exit_code;
  result.diagnostic = transcode.diagnostic;

  if (!transcode.success && selection.backend != EncoderBackend::software &&
      request.profile.allow_software_fallback) {
    auto fallback_profile = request.profile;
    fallback_profile.prefer_hardware = false;
    TranscodeRequest fallback_request = request;
    fallback_request.profile = fallback_profile;
    const auto software_selection = EncoderSelector::select(fallback_profile, capabilities);
    if (software_selection.supported && software_selection.backend == EncoderBackend::software) {
      auto fallback = runtime.transcode(fallback_request, EncoderBackend::software);
      result.success = fallback.success;
      result.executed_backend = EncoderBackend::software;
      result.used_fallback = true;
      result.process_exit_code = fallback.exit_code;
      result.diagnostic = fallback.success ? "hardware encoder failed; software fallback succeeded"
                                           : "hardware and software encoder attempts failed";
    }
  }
  return result;
}

}  // namespace digitor
