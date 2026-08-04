#include "digitor/plugin_gpu_program.hpp"

#include <algorithm>
#include <utility>

namespace digitor {
namespace {

std::string key_for(std::string_view id, std::string_view version,
                    RemotePluginBackend backend,
                    PluginGpuProgramFormat format) {
  return std::string(id) + "\n" + std::string(version) + "\n" +
         std::to_string(static_cast<std::uint32_t>(backend)) + "\n" +
         std::to_string(static_cast<std::uint32_t>(format));
}

bool valid_program(const PluginGpuProgram& program,
                   std::string& diagnostic) noexcept {
  if (program.plugin_id.empty() || program.plugin_version.empty() ||
      program.package_identity.empty() || program.passes.empty() ||
      program.passes.size() > 32) {
    diagnostic = "plugin GPU program identity or pass count is invalid";
    return false;
  }
  for (const auto& pass : program.passes) {
    if (pass.entry_point.empty() || pass.shader_asset.empty() ||
        pass.workgroup_x == 0 || pass.workgroup_y == 0 ||
        pass.workgroup_z == 0 || pass.workgroup_x > 1024 ||
        pass.workgroup_y > 1024 || pass.workgroup_z > 64 ||
        !pass.preserves_alpha || !pass.deterministic) {
      diagnostic = "plugin GPU pass violates production execution policy";
      return false;
    }
    if (pass.bindings.empty() || pass.bindings.size() > 32) {
      diagnostic = "plugin GPU pass binding count is invalid";
      return false;
    }
    bool input = false;
    bool output = false;
    for (const auto& binding : pass.bindings) {
      if (binding.name.empty()) {
        diagnostic = "plugin GPU binding name is missing";
        return false;
      }
      input = input || (!binding.writable && binding.name == "input");
      output = output || (binding.writable && binding.name == "output");
    }
    if (!input || !output) {
      diagnostic = "plugin GPU pass requires input SRV and output UAV bindings";
      return false;
    }
  }
  diagnostic.clear();
  return true;
}

}  // namespace

PluginGpuProgramFormat plugin_program_format(PluginPixelFormat format) noexcept {
  switch (format) {
    case PluginPixelFormat::rgba8_unorm:
      return PluginGpuProgramFormat::rgba8_unorm;
    case PluginPixelFormat::bgra8_unorm:
      return PluginGpuProgramFormat::bgra8_unorm;
    case PluginPixelFormat::rgba16_float:
      return PluginGpuProgramFormat::rgba16_float;
    case PluginPixelFormat::rgba32_float:
      return PluginGpuProgramFormat::rgba32_float;
  }
  return PluginGpuProgramFormat::rgba8_unorm;
}

DigitorResult PluginGpuProgramRegistry::register_program(
    PluginGpuProgram program, std::string* diagnostic) noexcept {
  std::string local;
  if (!valid_program(program, local)) {
    if (diagnostic) *diagnostic = local;
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  programs_[key_for(program.plugin_id, program.plugin_version,
                    program.backend, program.format)] = std::move(program);
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

std::optional<PluginGpuProgram> PluginGpuProgramRegistry::resolve(
    std::string_view plugin_id, std::string_view plugin_version,
    RemotePluginBackend backend, PluginGpuProgramFormat format) const {
  const auto it = programs_.find(key_for(plugin_id, plugin_version,
                                         backend, format));
  return it == programs_.end() ? std::nullopt
                               : std::optional<PluginGpuProgram>(it->second);
}

void PluginGpuProgramRegistry::unregister_plugin(std::string_view plugin_id) {
  for (auto it = programs_.begin(); it != programs_.end();) {
    if (it->second.plugin_id == plugin_id) it = programs_.erase(it);
    else ++it;
  }
}

PluginGpuProgramRuntime::PluginGpuProgramRuntime(
    const PluginGpuProgramRegistry& registry,
    PluginGpuProgramRuntimeBindings bindings)
    : registry_(registry), bindings_(std::move(bindings)) {}

DigitorResult PluginGpuProgramRuntime::dispatch(
    const PluginZeroCopyRequest& request,
    std::string* diagnostic) const noexcept {
  if (request.input.backend != bindings_.selected_backend ||
      request.output.backend != bindings_.selected_backend) {
    if (diagnostic) *diagnostic =
        "plugin GPU request differs from selected backend";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  const auto program = registry_.resolve(
      request.instance.plugin_id, request.instance.plugin_version,
      bindings_.selected_backend, plugin_program_format(request.input.format));
  if (!program) {
    if (diagnostic) *diagnostic =
        "compatible plugin GPU program is not registered";
    return DIGITOR_RESULT_UNSUPPORTED;
  }
  if (!bindings_.record_pass) {
    if (diagnostic) *diagnostic = "plugin GPU record callback is unavailable";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (program->passes.size() != 1) {
    if (diagnostic) *diagnostic =
        "multi-pass plugin requires stack-provided GPU intermediates";
    return DIGITOR_RESULT_UNSUPPORTED;
  }

  PluginGpuDispatchPass pass{};
  pass.program = *program;
  pass.pass = program->passes.front();
  pass.input = request.input;
  pass.output = request.output;
  pass.parameters = request.instance.parameters;
  std::string local;
  const auto result = bindings_.record_pass(pass, local);
  if (result != DIGITOR_RESULT_OK) {
    if (diagnostic) *diagnostic = local.empty()
        ? "plugin GPU backend dispatch failed without fallback" : local;
    return result;
  }
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

}  // namespace digitor
