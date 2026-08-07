#include "digitor/hardware_export_runtime.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>

using namespace digitor;

namespace {
const EncoderCapability* find_capability(const std::vector<EncoderCapability>& capabilities,
                                         EncoderBackend backend) {
  const auto it = std::find_if(capabilities.begin(), capabilities.end(),
                               [backend](const EncoderCapability& capability) {
                                 return capability.backend == backend;
                               });
  return it == capabilities.end() ? nullptr : &*it;
}

bool supports(const EncoderCapability& capability, ExportCodec codec) {
  return std::find(capability.codecs.begin(), capability.codecs.end(), codec) !=
         capability.codecs.end();
}
}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "digitor_hw_export_test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  TextCommandExecutor probe = [](const std::vector<std::string>& args, std::string& output) {
    assert(args.size() == 3);
    output = "Encoders:\n V..... h264_nvenc NVIDIA NVENC H.264 encoder\n"
             " V..... hevc_qsv Intel Quick Sync HEVC encoder\n"
             " V..... libx264 libx264 H.264 encoder\n"
             " V..... libx265 libx265 HEVC encoder\n";
    return 0;
  };

  const auto inventory = probe_ffmpeg_encoder_inventory("ffmpeg", probe);
  assert(inventory.ffmpeg_available);
  assert(inventory.encoder_names.size() == 4);
  const auto capabilities = capabilities_from_inventory(inventory);
  assert(capabilities.size() == 3);

  const auto* nvenc = find_capability(capabilities, EncoderBackend::nvenc);
  const auto* qsv = find_capability(capabilities, EncoderBackend::quick_sync);
  const auto* software = find_capability(capabilities, EncoderBackend::software);
  assert(nvenc && qsv && software);
  assert(supports(*nvenc, ExportCodec::h264));
  assert(!supports(*nvenc, ExportCodec::hevc));
  assert(!supports(*nvenc, ExportCodec::av1));
  assert(!supports(*qsv, ExportCodec::h264));
  assert(supports(*qsv, ExportCodec::hevc));
  assert(!supports(*qsv, ExportCodec::av1));
  assert(supports(*software, ExportCodec::h264));
  assert(supports(*software, ExportCodec::hevc));

  int attempts = 0;
  ProcessExecutor failing_hardware = [&attempts](const std::vector<std::string>&) {
    ++attempts;
    return 1;
  };

  HardwareAwareExportRuntime runtime("ffmpeg", probe, failing_hardware);
  TranscodeRequest request;
  request.input_path = root / "input.mp4";
  request.output_path = root / "output.mp4";
  request.profile.codec = ExportCodec::h264;
  request.profile.prefer_hardware = true;
  request.profile.allow_software_fallback = true;
  std::ofstream(request.input_path) << "source";

  const auto failed = runtime.execute(request);
  assert(!failed.success);
  assert(failed.requested_backend == EncoderBackend::nvenc);
  assert(failed.executed_backend == EncoderBackend::nvenc);
  assert(!failed.used_fallback);
  assert(attempts == 1);
  assert(!std::filesystem::exists(request.output_path));

  // CPU fallback remains valid when no compatible GPU/hardware encoder exists
  // at selection time.
  TextCommandExecutor software_only = [](const std::vector<std::string>&, std::string& output) {
    output = "Encoders:\n V..... libx264 libx264 H.264 encoder\n";
    return 0;
  };
  int software_attempts = 0;
  ProcessExecutor software_process = [&software_attempts](const std::vector<std::string>& args) {
    ++software_attempts;
    std::ofstream out(args.back(), std::ios::binary);
    out << "encoded";
    return out ? 0 : 2;
  };
  HardwareAwareExportRuntime fallback_runtime("ffmpeg", software_only, software_process);
  const auto fallback = fallback_runtime.execute(request);
  assert(fallback.success);
  assert(fallback.requested_backend == EncoderBackend::software);
  assert(fallback.executed_backend == EncoderBackend::software);
  assert(fallback.used_fallback);
  assert(software_attempts == 1);

  TextCommandExecutor missing = [](const std::vector<std::string>&, std::string&) { return 127; };
  const auto unavailable = probe_ffmpeg_encoder_inventory("missing", missing);
  assert(!unavailable.ffmpeg_available);

  std::filesystem::remove_all(root);
  return 0;
}
