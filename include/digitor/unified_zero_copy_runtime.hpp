#pragma once

#include "digitor/digitor.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

enum class ZeroCopyPlatform : std::uint32_t { windows=1, android=2, apple=3 };
enum class ZeroCopyRuntimeState : std::uint32_t { disabled=0, qualified=1, active=2, quarantined=3 };

struct ZeroCopyQualificationEvidence {
  ZeroCopyPlatform platform{ZeroCopyPlatform::windows};
  std::string device_identity;
  std::string driver_or_os_build;
  std::string engine_commit;
  std::string qualification_id;
  std::int64_t valid_until_unix_seconds{};
  bool strict_gpu_first{};
  bool decode_zero_copy{};
  bool render_zero_copy{};
  bool preview_export_identity{};
  bool per_pixel_accuracy{};
  bool hardware_encode{};
  bool sustained_4k{};
  bool stress_and_leak{};
  double measured_fps{};
  double minimum_fps{};
  double max_mean_error{};
  double allowed_mean_error{};
  std::int64_t resource_delta{};
  std::int64_t allowed_resource_delta{};
};

struct UnifiedZeroCopyConfig {
  ZeroCopyPlatform platform{ZeroCopyPlatform::windows};
  std::string device_identity;
  std::string driver_or_os_build;
  std::string engine_commit;
  bool require_strict_gpu_first{true};
  bool require_preview_export_identity{true};
  std::uint32_t quarantine_after_failures{3};
};

struct UnifiedZeroCopyTelemetry {
  ZeroCopyRuntimeState state{ZeroCopyRuntimeState::disabled};
  std::uint64_t preview_frames{};
  std::uint64_t export_frames{};
  std::uint64_t shared_frame_reuses{};
  std::uint64_t failures{};
  std::uint64_t cpu_copies{};
  std::uint64_t cpu_fallback_frames{};
  std::string diagnostic;
};

using ZeroCopyFrameAction = std::function<DigitorResult(std::int64_t)>;
using ZeroCopyResetAction = std::function<DigitorResult()>;

struct UnifiedZeroCopyBinding {
  ZeroCopyFrameAction preview;
  ZeroCopyFrameAction export_frame;
  ZeroCopyFrameAction preview_and_export;
  ZeroCopyResetAction reset_platform_quarantine;
};

class UnifiedZeroCopyRuntime final {
public:
  UnifiedZeroCopyRuntime(UnifiedZeroCopyConfig, UnifiedZeroCopyBinding);
  ~UnifiedZeroCopyRuntime();
  UnifiedZeroCopyRuntime(const UnifiedZeroCopyRuntime&) = delete;
  UnifiedZeroCopyRuntime& operator=(const UnifiedZeroCopyRuntime&) = delete;

  [[nodiscard]] DigitorResult activate(const ZeroCopyQualificationEvidence&, std::int64_t now_unix_seconds) noexcept;
  [[nodiscard]] DigitorResult preview(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult export_frame(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult preview_and_export(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult reset_quarantine(const ZeroCopyQualificationEvidence&, std::int64_t now_unix_seconds) noexcept;
  [[nodiscard]] UnifiedZeroCopyTelemetry telemetry() const;
  [[nodiscard]] bool production_ready() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] DigitorResult validate_zero_copy_evidence(
    const UnifiedZeroCopyConfig&, const ZeroCopyQualificationEvidence&,
    std::int64_t now_unix_seconds, std::string& diagnostic) noexcept;

} // namespace digitor
