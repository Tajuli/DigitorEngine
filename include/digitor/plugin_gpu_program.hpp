#pragma once

#include "digitor/plugin_zero_copy_frame.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class PluginGpuProgramFormat : std::uint32_t {
  rgba8_unorm,
  bgra8_unorm,
  rgba16_float,
  rgba32_float
};

struct PluginGpuBinding final {
  std::string name;
  std::uint32_t slot{};
  bool writable{};
};

struct PluginGpuPassDescriptor final {
  std::string entry_point;
  std::string shader_asset;
  std::vector<PluginGpuBinding> bindings;
  std::uint32_t workgroup_x{8};
  std::uint32_t workgroup_y{8};
  std::uint32_t workgroup_z{1};
  bool preserves_alpha{true};
  bool deterministic{true};
};

struct PluginGpuProgram final {
  std::string plugin_id;
  std::string plugin_version;
  RemotePluginBackend backend{RemotePluginBackend::windows_d3d12};
  PluginGpuProgramFormat format{PluginGpuProgramFormat::rgba8_unorm};
  std::string package_identity;
  std::vector<PluginGpuPassDescriptor> passes;
};

class PluginGpuProgramRegistry final {
 public:
  [[nodiscard]] DigitorResult register_program(
      PluginGpuProgram program, std::string* diagnostic = nullptr) noexcept;
  [[nodiscard]] std::optional<PluginGpuProgram> resolve(
      std::string_view plugin_id, std::string_view plugin_version,
      RemotePluginBackend backend, PluginGpuProgramFormat format) const;
  void unregister_plugin(std::string_view plugin_id);

 private:
  std::unordered_map<std::string, PluginGpuProgram> programs_;
};

struct PluginGpuDispatchPass final {
  PluginGpuProgram program;
  PluginGpuPassDescriptor pass;
  std::uint32_t pass_index{};
  PluginGpuFrame input;
  PluginGpuFrame output;
  std::unordered_map<std::string, double> parameters;
};

using PluginGpuRecordPass = std::function<DigitorResult(
    const PluginGpuDispatchPass&, std::string& diagnostic)>;

struct PluginGpuProgramRuntimeBindings final {
  RemotePluginBackend selected_backend{RemotePluginBackend::windows_d3d12};
  PluginGpuRecordPass record_pass;
};

class PluginGpuProgramRuntime final {
 public:
  PluginGpuProgramRuntime(const PluginGpuProgramRegistry& registry,
                          PluginGpuProgramRuntimeBindings bindings);

  [[nodiscard]] DigitorResult dispatch(
      const PluginZeroCopyRequest& request,
      std::string* diagnostic = nullptr) const noexcept;

 private:
  const PluginGpuProgramRegistry& registry_;
  PluginGpuProgramRuntimeBindings bindings_;
};

[[nodiscard]] PluginGpuProgramFormat plugin_program_format(
    PluginPixelFormat format) noexcept;

}  // namespace digitor
