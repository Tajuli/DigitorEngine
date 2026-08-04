#pragma once

#include "digitor/plugin_gpu_program.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class PluginShaderBinaryKind : std::uint32_t {
  dxil,
  spirv,
  metallib,
  glsl_es
};

struct PluginBackendAsset final {
  std::string package_identity;
  std::string plugin_id;
  std::string plugin_version;
  RemotePluginBackend backend{RemotePluginBackend::windows_d3d12};
  PluginGpuProgramFormat format{PluginGpuProgramFormat::rgba8_unorm};
  PluginShaderBinaryKind binary_kind{PluginShaderBinaryKind::dxil};
  std::string relative_path;
  std::vector<std::byte> bytes;
};

struct PluginBackendPipeline final {
  std::string package_identity;
  std::string plugin_id;
  std::string plugin_version;
  RemotePluginBackend backend{RemotePluginBackend::windows_d3d12};
  PluginGpuProgramFormat format{PluginGpuProgramFormat::rgba8_unorm};
  std::uint64_t native_pipeline_handle{};
  std::uint64_t device_identity{};
};

using PluginBackendReadAsset = std::function<bool(
    std::string_view installed_root, std::string_view relative_path,
    std::vector<std::byte>& bytes, std::string& diagnostic)>;
using PluginBackendCreatePipeline = std::function<DigitorResult(
    const PluginBackendAsset&, const PluginGpuProgram&,
    PluginBackendPipeline&, std::string& diagnostic)>;
using PluginBackendDestroyPipeline = std::function<void(
    const PluginBackendPipeline&)>;

struct PluginBackendPackageLoaderBindings final {
  RemotePluginBackend selected_backend{RemotePluginBackend::windows_d3d12};
  std::uint64_t device_identity{};
  PluginBackendReadAsset read_asset;
  PluginBackendCreatePipeline create_pipeline;
  PluginBackendDestroyPipeline destroy_pipeline;
};

class PluginBackendPackageLoader final {
 public:
  PluginBackendPackageLoader(PluginGpuProgramRegistry& registry,
                             PluginBackendPackageLoaderBindings bindings);
  ~PluginBackendPackageLoader();

  [[nodiscard]] DigitorResult load(
      std::string_view installed_root, PluginGpuProgram program,
      std::string* diagnostic = nullptr) noexcept;
  void unload(std::string_view plugin_id) noexcept;
  [[nodiscard]] const PluginBackendPipeline* pipeline(
      std::string_view plugin_id, std::string_view plugin_version,
      PluginGpuProgramFormat format) const noexcept;

 private:
  std::string key(std::string_view plugin_id,
                  std::string_view plugin_version,
                  PluginGpuProgramFormat format) const;

  PluginGpuProgramRegistry& registry_;
  PluginBackendPackageLoaderBindings bindings_;
  std::unordered_map<std::string, PluginBackendPipeline> pipelines_;
};

[[nodiscard]] PluginShaderBinaryKind plugin_shader_binary_kind(
    RemotePluginBackend backend) noexcept;

}  // namespace digitor
