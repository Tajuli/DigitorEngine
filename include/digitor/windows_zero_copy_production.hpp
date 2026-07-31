#pragma once

#include "digitor/windows_d3d12_p010_dispatch.hpp"
#include "digitor/windows_d3d12_p010_converter.hpp"
#include "digitor/windows_zero_copy_concrete_bindings.hpp"
#include "digitor/windows_zero_copy_native_consumers.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

struct WindowsZeroCopyProductionEvidence {
  bool production_ready{};
  bool strict_gpu_first{};
  bool nv12_pass{};
  bool p010_pass{};
  bool preview_export_identity_pass{};
  bool per_pixel_accuracy_pass{};
  bool sustained_4k_pass{};
  bool leak_test_pass{};
  bool hevc_main10_pass{};
  std::uint64_t adapter_luid{};
  std::uint64_t driver_version{};
  std::string engine_commit;
  std::string qualification_id;
  std::int64_t expires_unix_seconds{};
  double minimum_fps{30.0};
  double measured_fps{};
  double maximum_mean_error{0.0005};
  double measured_mean_error{};
  std::uint64_t maximum_resource_delta{};
  std::uint64_t measured_resource_delta{};
};

struct WindowsZeroCopyProductionConfig {
  WindowsZeroCopyRuntimeConfig runtime;
  WindowsZeroCopyNativeBinding native_binding;
  WindowsP010ConversionConfig p010_conversion;
  WindowsD3D12P010DispatchConfig p010_dispatch;
  WindowsMediaFoundationEncoderConfig encoder;
  WindowsZeroCopyProductionEvidence evidence;
  std::uint64_t current_adapter_luid{};
  std::uint64_t current_driver_version{};
  std::string current_engine_commit;
  std::int64_t current_unix_seconds{};
  bool enable_preview{true};
  bool enable_export{true};
};

struct WindowsZeroCopyProductionTelemetry {
  bool initialized{};
  bool quarantined{};
  std::uint64_t preview_frames{};
  std::uint64_t exported_frames{};
  std::uint64_t activation_failures{};
  std::uint64_t runtime_failures{};
  std::uint64_t cpu_copies{};
  std::uint64_t cpu_fallback_frames{};
  std::string diagnostic;
};

class WindowsZeroCopyProductionPipeline final {
public:
  explicit WindowsZeroCopyProductionPipeline(WindowsZeroCopyProductionConfig);
  ~WindowsZeroCopyProductionPipeline();

  WindowsZeroCopyProductionPipeline(const WindowsZeroCopyProductionPipeline&) = delete;
  WindowsZeroCopyProductionPipeline& operator=(const WindowsZeroCopyProductionPipeline&) = delete;

  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult preview(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult export_frame(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult finalize_export() noexcept;
  void quarantine(std::string reason) noexcept;
  [[nodiscard]] WindowsZeroCopyProductionTelemetry telemetry() const;

  [[nodiscard]] static DigitorResult validate_evidence(
      const WindowsZeroCopyProductionConfig&, std::string& diagnostic) noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace digitor
