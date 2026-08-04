#include "digitor/plugin_backend_package_loader.hpp"

#include <algorithm>
#include <utility>

namespace digitor {
namespace {

bool safe_relative_path(std::string_view path) noexcept {
  return !path.empty() && path.size() <= 512 &&
         path.front() != '/' && path.front() != '\\' &&
         path.find("..") == std::string_view::npos &&
         path.find(':') == std::string_view::npos;
}

bool valid_binary(const PluginBackendAsset& asset,
                  std::string& diagnostic) noexcept {
  if (asset.bytes.empty() || asset.bytes.size() > 64u * 1024u * 1024u) {
    diagnostic = "plugin shader asset is empty or oversized";
    return false;
  }
  if (asset.binary_kind == PluginShaderBinaryKind::spirv) {
    if (asset.bytes.size() < 4 || asset.bytes.size() % 4 != 0) {
      diagnostic = "SPIR-V asset size is invalid";
      return false;
    }
    const auto* p = reinterpret_cast<const unsigned char*>(asset.bytes.data());
    if (!(p[0] == 0x03 && p[1] == 0x02 && p[2] == 0x23 && p[3] == 0x07)) {
      diagnostic = "SPIR-V magic is invalid";
      return false;
    }
  } else if (asset.binary_kind == PluginShaderBinaryKind::dxil) {
    if (asset.bytes.size() < 4) {
      diagnostic = "DXIL asset is truncated";
      return false;
    }
  } else if (asset.binary_kind == PluginShaderBinaryKind::metallib) {
    if (asset.bytes.size() < 16) {
      diagnostic = "metallib asset is truncated";
      return false;
    }
  } else {
    const auto* p = reinterpret_cast<const char*>(asset.bytes.data());
    const std::string_view text(p, asset.bytes.size());
    if (text.find("#version 300 es") == std::string_view::npos &&
        text.find("#version 310 es") == std::string_view::npos) {
      diagnostic = "GLSL ES asset lacks a supported version directive";
      return false;
    }
  }
  diagnostic.clear();
  return true;
}

}  // namespace

PluginShaderBinaryKind plugin_shader_binary_kind(
    RemotePluginBackend backend) noexcept {
  switch (backend) {
    case RemotePluginBackend::windows_d3d12:
      return PluginShaderBinaryKind::dxil;
    case RemotePluginBackend::windows_vulkan:
    case RemotePluginBackend::android_vulkan:
      return PluginShaderBinaryKind::spirv;
    case RemotePluginBackend::apple_metal:
      return PluginShaderBinaryKind::metallib;
    case RemotePluginBackend::android_gles:
      return PluginShaderBinaryKind::glsl_es;
  }
  return PluginShaderBinaryKind::dxil;
}

PluginBackendPackageLoader::PluginBackendPackageLoader(
    PluginGpuProgramRegistry& registry,
    PluginBackendPackageLoaderBindings bindings)
    : registry_(registry), bindings_(std::move(bindings)) {}

PluginBackendPackageLoader::~PluginBackendPackageLoader() {
  if (bindings_.destroy_pipeline) {
    for (const auto& [_, value] : pipelines_)
      bindings_.destroy_pipeline(value);
  }
}

std::string PluginBackendPackageLoader::key(
    std::string_view plugin_id, std::string_view plugin_version,
    PluginGpuProgramFormat format) const {
  return std::string(plugin_id) + "\n" + std::string(plugin_version) + "\n" +
         std::to_string(static_cast<std::uint32_t>(format));
}

DigitorResult PluginBackendPackageLoader::load(
    std::string_view installed_root, PluginGpuProgram program,
    std::string* diagnostic) noexcept {
  std::string local;
  auto fail = [&](DigitorResult result, std::string message) {
    if (diagnostic) *diagnostic = std::move(message);
    return result;
  };

  if (installed_root.empty() || program.plugin_id.empty() ||
      program.plugin_version.empty() || program.package_identity.empty() ||
      program.backend != bindings_.selected_backend ||
      bindings_.device_identity == 0 || !bindings_.read_asset ||
      !bindings_.create_pipeline || !bindings_.destroy_pipeline) {
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                "plugin backend package loader bindings or identity are invalid");
  }
  if (program.passes.empty() || program.passes.size() > 32) {
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                "plugin backend program pass count is invalid");
  }

  std::string shader_path;
  for (const auto& pass : program.passes) {
    if (!safe_relative_path(pass.shader_asset) || pass.entry_point.empty()) {
      return fail(DIGITOR_RESULT_INVALID_ARGUMENT,
                  "plugin shader asset path or entry point is invalid");
    }
    if (shader_path.empty()) shader_path = pass.shader_asset;
    if (shader_path != pass.shader_asset) {
      return fail(DIGITOR_RESULT_UNSUPPORTED,
                  "one backend program must use one validated shader asset");
    }
  }

  PluginBackendAsset asset{};
  asset.package_identity = program.package_identity;
  asset.plugin_id = program.plugin_id;
  asset.plugin_version = program.plugin_version;
  asset.backend = program.backend;
  asset.format = program.format;
  asset.binary_kind = plugin_shader_binary_kind(program.backend);
  asset.relative_path = shader_path;
  if (!bindings_.read_asset(installed_root, shader_path, asset.bytes, local)) {
    return fail(DIGITOR_RESULT_BACKEND_UNAVAILABLE,
                local.empty() ? "plugin shader asset read failed" : local);
  }
  if (!valid_binary(asset, local)) {
    return fail(DIGITOR_RESULT_INVALID_ARGUMENT, local);
  }

  PluginBackendPipeline pipeline{};
  const auto created = bindings_.create_pipeline(asset, program, pipeline, local);
  if (created != DIGITOR_RESULT_OK || pipeline.native_pipeline_handle == 0 ||
      pipeline.device_identity != bindings_.device_identity ||
      pipeline.plugin_id != program.plugin_id ||
      pipeline.plugin_version != program.plugin_version ||
      pipeline.package_identity != program.package_identity ||
      pipeline.backend != program.backend || pipeline.format != program.format) {
    if (pipeline.native_pipeline_handle != 0)
      bindings_.destroy_pipeline(pipeline);
    return fail(created == DIGITOR_RESULT_OK
                    ? DIGITOR_RESULT_BACKEND_UNAVAILABLE
                    : created,
                local.empty()
                    ? "plugin backend pipeline violated device/package identity"
                    : local);
  }

  const auto registry_result = registry_.register_program(program, &local);
  if (registry_result != DIGITOR_RESULT_OK) {
    bindings_.destroy_pipeline(pipeline);
    return fail(registry_result, local);
  }

  const auto k = key(program.plugin_id, program.plugin_version, program.format);
  const auto old = pipelines_.find(k);
  if (old != pipelines_.end()) {
    bindings_.destroy_pipeline(old->second);
    old->second = std::move(pipeline);
  } else {
    pipelines_.emplace(k, std::move(pipeline));
  }
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

void PluginBackendPackageLoader::unload(std::string_view plugin_id) noexcept {
  for (auto it = pipelines_.begin(); it != pipelines_.end();) {
    if (it->second.plugin_id == plugin_id) {
      if (bindings_.destroy_pipeline)
        bindings_.destroy_pipeline(it->second);
      it = pipelines_.erase(it);
    } else {
      ++it;
    }
  }
  registry_.unregister_plugin(plugin_id);
}

const PluginBackendPipeline* PluginBackendPackageLoader::pipeline(
    std::string_view plugin_id, std::string_view plugin_version,
    PluginGpuProgramFormat format) const noexcept {
  const auto it = pipelines_.find(key(plugin_id, plugin_version, format));
  return it == pipelines_.end() ? nullptr : &it->second;
}

}  // namespace digitor
