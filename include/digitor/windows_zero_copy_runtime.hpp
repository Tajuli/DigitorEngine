#pragma once

#include "digitor/digitor.h"
#include "digitor/gpu_frame.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace digitor {

enum class WindowsZeroCopyRuntimeState : std::uint32_t {
  disabled = 0,
  evidence_rejected,
  ready,
  active,
  quarantined,
  device_lost
};

enum class WindowsZeroCopyFailureClass : std::uint32_t {
  none = 0,
  unsupported_surface,
  evidence_mismatch,
  decoder_failure,
  import_failure,
  color_conversion_failure,
  timestamp_mismatch,
  device_removed,
  timeout,
  integrity_failure
};

struct WindowsZeroCopyRuntimeConfig {
  std::string evidence_path;
  std::string adapter_luid;
  std::string driver_version;
  std::string engine_commit;
  bool enabled{};
  bool strict_gpu_first{true};
  bool allow_session_quarantine{true};
  std::uint32_t consecutive_failure_limit{3};
  std::uint32_t frame_timeout_ms{250};
};

struct WindowsZeroCopyRuntimeTelemetry {
  WindowsZeroCopyRuntimeState state{WindowsZeroCopyRuntimeState::disabled};
  WindowsZeroCopyFailureClass last_failure{WindowsZeroCopyFailureClass::none};
  std::uint64_t frames_requested{};
  std::uint64_t frames_completed{};
  std::uint64_t frames_rejected{};
  std::uint64_t zero_copy_frames{};
  std::uint64_t cpu_fallback_frames{};
  std::uint64_t timestamp_mismatches{};
  std::uint64_t device_loss_events{};
  std::uint32_t consecutive_failures{};
  std::string diagnostic;
};

using WindowsZeroCopyDecodeCallback = std::function<DigitorResult(
    std::int64_t timestamp_us, ProcessedGpuFramePtr&)>;
using WindowsZeroCopyFrameConsumer = std::function<DigitorResult(
    const ProcessedGpuFramePtr&)>;

class WindowsZeroCopyRuntime final {
public:
  WindowsZeroCopyRuntime(WindowsZeroCopyRuntimeConfig,
                         WindowsZeroCopyDecodeCallback,
                         WindowsZeroCopyFrameConsumer preview_consumer,
                         WindowsZeroCopyFrameConsumer export_consumer);
  ~WindowsZeroCopyRuntime();

  WindowsZeroCopyRuntime(const WindowsZeroCopyRuntime&) = delete;
  WindowsZeroCopyRuntime& operator=(const WindowsZeroCopyRuntime&) = delete;

  [[nodiscard]] DigitorResult initialize() noexcept;
  [[nodiscard]] DigitorResult render_preview(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult render_export(std::int64_t timestamp_us) noexcept;
  [[nodiscard]] DigitorResult reset_quarantine() noexcept;
  [[nodiscard]] WindowsZeroCopyRuntimeTelemetry telemetry() const;
  [[nodiscard]] bool production_active() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace digitor
