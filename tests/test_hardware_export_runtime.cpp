#include "digitor/hardware_export_runtime.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

using namespace digitor;

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

  int attempts = 0;
  ProcessExecutor process = [&attempts](const std::vector<std::string>& args) {
    ++attempts;
    bool software = false;
    for (const auto& arg : args) if (arg == "libx264") software = true;
    if (!software) return 1;
    std::ofstream out(args.back(), std::ios::binary);
    out << "encoded";
    return out ? 0 : 2;
  };

  HardwareAwareExportRuntime runtime("ffmpeg", probe, process);
  TranscodeRequest request;
  request.input_path = root / "input.mp4";
  request.output_path = root / "output.mp4";
  request.profile.codec = ExportCodec::h264;
  request.profile.prefer_hardware = true;
  request.profile.allow_software_fallback = true;
  std::ofstream(request.input_path) << "source";

  const auto result = runtime.execute(request);
  assert(result.success);
  assert(result.requested_backend == EncoderBackend::nvenc);
  assert(result.executed_backend == EncoderBackend::software);
  assert(result.used_fallback);
  assert(attempts == 2);
  assert(std::filesystem::exists(request.output_path));

  TextCommandExecutor missing = [](const std::vector<std::string>&, std::string&) { return 127; };
  const auto unavailable = probe_ffmpeg_encoder_inventory("missing", missing);
  assert(!unavailable.ffmpeg_available);

  std::filesystem::remove_all(root);
  return 0;
}
