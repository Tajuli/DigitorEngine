#pragma once

#include "digitor/plugin_gpu_program.hpp"
#include "digitor/plugin_transition_runtime.hpp"

#include <string>

namespace digitor {

struct PluginTransitionProgramBinding final {
  const PluginGpuProgramRegistry* registry{};
  RemotePluginBackend selected_backend{RemotePluginBackend::windows_d3d12};
  PluginGpuRecordPass record_pass;
};

class PluginTransitionProgramRuntime final {
 public:
  explicit PluginTransitionProgramRuntime(PluginTransitionProgramBinding binding);

  [[nodiscard]] DigitorResult dispatch(
      const PluginTransitionRequest& request,
      std::string* diagnostic = nullptr) const noexcept;

 private:
  PluginTransitionProgramBinding binding_;
};

[[nodiscard]] bool validate_transition_program_contract(
    const PluginGpuProgram& program,
    std::string& diagnostic) noexcept;

}  // namespace digitor
