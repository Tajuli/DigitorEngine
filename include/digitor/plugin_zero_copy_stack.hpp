#pragma once

#include "digitor/plugin_zero_copy_frame.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

struct PluginZeroCopyStackRequest final {
  ConsumerPluginSurface surface{ConsumerPluginSurface::preview};
  std::string project_or_clip_id;
  std::string visual_stack_digest;
  std::vector<ConsumerPluginInstance> instances;
  PluginGpuFrame source;
  PluginGpuFrame destination;
  std::vector<PluginGpuFrame> intermediates;
};

struct PluginZeroCopyStackTelemetry final {
  std::uint64_t stack_frames{};
  std::uint64_t plugin_dispatches{};
  std::uint64_t preview_frames{};
  std::uint64_t export_frames{};
  std::uint64_t cpu_readbacks{};
  std::uint64_t cpu_uploads{};
  std::uint64_t cpu_fallback_frames{};
  std::uint64_t failed_frames{};
  std::string diagnostic;
};

using PluginZeroCopyInstanceResolver = std::function<bool(
    const ConsumerPluginInstance&, std::string& diagnostic)>;

class PluginZeroCopyStackRuntime final {
 public:
  PluginZeroCopyStackRuntime(PluginZeroCopyFrameRuntime& frame_runtime,
                             PluginZeroCopyInstanceResolver resolver = {});

  [[nodiscard]] DigitorResult process(
      const PluginZeroCopyStackRequest& request,
      std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] PluginZeroCopyStackTelemetry telemetry() const;

 private:
  bool validate_request(const PluginZeroCopyStackRequest& request,
                        std::string& diagnostic) const noexcept;

  PluginZeroCopyFrameRuntime& frame_runtime_;
  PluginZeroCopyInstanceResolver resolver_;
  PluginZeroCopyStackTelemetry telemetry_;
};

}  // namespace digitor
