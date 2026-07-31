#pragma once

#include <string>

namespace digitor {

enum class WindowsZeroCopyRolloutMode { disabled, qualification_only, production };

struct WindowsZeroCopyRolloutDecision {
  WindowsZeroCopyRolloutMode requested{WindowsZeroCopyRolloutMode::disabled};
  WindowsZeroCopyRolloutMode effective{WindowsZeroCopyRolloutMode::disabled};
  bool report_loaded{};
  bool production_ready{};
  bool strict_gpu_first{};
  std::string diagnostic;
};

[[nodiscard]] WindowsZeroCopyRolloutDecision evaluate_windows_zero_copy_rollout(
    WindowsZeroCopyRolloutMode requested,
    const std::string& qualification_report_path,
    bool strict_gpu_first=true) noexcept;

} // namespace digitor
