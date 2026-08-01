#pragma once

#include "digitor/ffmpeg_export_runtime.hpp"
#include "digitor/production_export.hpp"

#include <functional>
#include <string>
#include <vector>

namespace digitor {

struct RuntimeEncoderInventory {
  bool ffmpeg_available{};
  std::vector<std::string> encoder_names;
  std::string diagnostic;
};

struct HardwareExportResult {
  bool success{};
  EncoderBackend requested_backend{EncoderBackend::software};
  EncoderBackend executed_backend{EncoderBackend::software};
  bool used_fallback{};
  int process_exit_code{-1};
  std::string diagnostic;
};

using TextCommandExecutor = std::function<int(const std::vector<std::string>&, std::string&)>;

[[nodiscard]] RuntimeEncoderInventory probe_ffmpeg_encoder_inventory(
    const std::string& ffmpeg_binary = "ffmpeg",
    TextCommandExecutor executor = {});

[[nodiscard]] std::vector<EncoderCapability> capabilities_from_inventory(
    const RuntimeEncoderInventory& inventory) noexcept;

class HardwareAwareExportRuntime final {
 public:
  explicit HardwareAwareExportRuntime(std::string ffmpeg_binary = "ffmpeg",
                                      TextCommandExecutor probe_executor = {},
                                      ProcessExecutor process_executor = {});

  [[nodiscard]] RuntimeEncoderInventory probe() const;
  [[nodiscard]] HardwareExportResult execute(
      const TranscodeRequest& request,
      const std::vector<EncoderCapability>& extra_capabilities = {}) const;

 private:
  std::string ffmpeg_binary_;
  TextCommandExecutor probe_executor_;
  ProcessExecutor process_executor_;
};

}  // namespace digitor
