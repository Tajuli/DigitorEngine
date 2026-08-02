#pragma once

#include "digitor/gpu_frame.hpp"
#include "digitor/production_export.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace digitor {

enum class HardwareEncodeState : std::uint32_t {
  idle, running, draining, completed, cancelled, failed
};

struct HardwareEncodeConfig {
  ExportProfile profile;
  EncoderBackend backend{EncoderBackend::software};
  std::string output_path;
  std::int64_t duration_us{};
  bool require_hardware{true};
  bool require_zero_copy{true};
  bool require_monotonic_timestamps{true};
  bool require_atomic_finalize{true};
};

struct HardwareEncodeFrame {
  ProcessedGpuFramePtr frame;
  std::int64_t pts_us{};
  std::int64_t duration_us{};
  bool force_keyframe{};
};

struct HardwareEncodeTelemetry {
  HardwareEncodeState state{HardwareEncodeState::idle};
  EncoderBackend backend{EncoderBackend::software};
  std::uint64_t submitted_frames{};
  std::uint64_t accepted_frames{};
  std::uint64_t rejected_frames{};
  std::uint64_t cpu_readbacks{};
  std::int64_t last_pts_us{-1};
  std::int64_t completed_us{};
  double progress{};
  std::string diagnostic;
};

struct HardwareEncoderCallbacks {
  std::function<DigitorResult(const HardwareEncodeConfig&, std::string&)> open;
  std::function<DigitorResult(const HardwareEncodeFrame&, std::string&)> submit_gpu_frame;
  std::function<DigitorResult(std::string&)> drain;
  std::function<DigitorResult(std::string&)> finalize_atomic;
  std::function<void()> cancel;
};

class ProductionHardwareEncodeSession final {
 public:
  ProductionHardwareEncodeSession(HardwareEncodeConfig config,
                                  HardwareEncoderCallbacks callbacks);
  ~ProductionHardwareEncodeSession();

  ProductionHardwareEncodeSession(const ProductionHardwareEncodeSession&) = delete;
  ProductionHardwareEncodeSession& operator=(const ProductionHardwareEncodeSession&) = delete;

  [[nodiscard]] DigitorResult start(std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] DigitorResult submit(HardwareEncodeFrame frame,
                                     std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] DigitorResult finish(std::string* diagnostic = nullptr) noexcept;
  void cancel() noexcept;
  [[nodiscard]] HardwareEncodeTelemetry telemetry() const;

 private:
  DigitorResult fail_locked(std::string diagnostic, std::string* output) noexcept;
  static bool hardware_backend(EncoderBackend backend) noexcept;

  HardwareEncodeConfig config_;
  HardwareEncoderCallbacks callbacks_;
  mutable std::mutex mutex_;
  HardwareEncodeTelemetry telemetry_;
  bool opened_{};
};

}  // namespace digitor
