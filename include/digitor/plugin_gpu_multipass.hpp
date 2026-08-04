#pragma once

#include "digitor/plugin_gpu_program.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace digitor {

using PluginGpuAllocateIntermediate = std::function<DigitorResult(
    const PluginGpuFrame& prototype, PluginGpuFrame& output,
    std::string& diagnostic)>;
using PluginGpuReleaseIntermediate = std::function<void(
    const PluginGpuFrame& frame)>;
using PluginGpuSubmit = std::function<DigitorResult(
    std::string& diagnostic)>;

struct PluginGpuMultiPassBindings final {
  RemotePluginBackend selected_backend{RemotePluginBackend::windows_d3d12};
  PluginGpuAllocateIntermediate allocate_intermediate;
  PluginGpuReleaseIntermediate release_intermediate;
  PluginGpuRecordPass record_pass;
  PluginGpuSubmit submit;
};

struct PluginGpuMultiPassTelemetry final {
  std::uint64_t dispatched_programs{};
  std::uint64_t recorded_passes{};
  std::uint64_t intermediate_allocations{};
  std::uint64_t submissions{};
  std::uint64_t failed_programs{};
  std::uint64_t cpu_readbacks{};
  std::uint64_t cpu_uploads{};
  std::uint64_t fallback_dispatches{};
  std::string diagnostic;
};

class PluginGpuMultiPassRuntime final {
 public:
  PluginGpuMultiPassRuntime(const PluginGpuProgramRegistry& registry,
                            PluginGpuMultiPassBindings bindings);

  [[nodiscard]] DigitorResult dispatch(
      const PluginZeroCopyRequest& request,
      std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] PluginGpuMultiPassTelemetry telemetry() const;

 private:
  const PluginGpuProgramRegistry& registry_;
  PluginGpuMultiPassBindings bindings_;
  PluginGpuMultiPassTelemetry telemetry_;
};

}  // namespace digitor
