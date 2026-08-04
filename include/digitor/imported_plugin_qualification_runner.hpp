#pragma once

#include "digitor/imported_plugin_release_qualification.hpp"

#include <cstdint>
#include <string>

namespace digitor {

struct ImportedPluginFrameEvidence final {
  bool preview{};
  std::uint64_t compared_pixels{};
  double squared_error_sum{};
  double max_absolute_error{};
  std::uint64_t alpha_mismatches{};
  std::string visual_stack_digest;
  std::string package_identity;
};

struct ImportedPluginRuntimeTelemetrySample final {
  std::uint64_t cpu_readbacks{};
  std::uint64_t cpu_uploads{};
  std::uint64_t fallback_dispatches{};
};

class ImportedPluginQualificationRunner final {
 public:
  ImportedPluginQualificationRunner(std::string plugin_id,
                                    std::string plugin_version,
                                    std::string package_identity,
                                    RemotePluginBackend backend,
                                    PluginPixelFormat format,
                                    bool physical_gpu,
                                    bool software_adapter);

  void record_frame(const ImportedPluginFrameEvidence& frame) noexcept;
  void record_runtime_telemetry(
      const ImportedPluginRuntimeTelemetrySample& sample) noexcept;
  void record_device_loss_recovery(bool recovered) noexcept;
  void record_soak_frame() noexcept;

  [[nodiscard]] ImportedPluginQualificationEvidence evidence() const;
  [[nodiscard]] ImportedPluginQualificationResult qualify() const noexcept;

 private:
  ImportedPluginQualificationEvidence evidence_;
  long double squared_error_sum_{};
};

}  // namespace digitor
