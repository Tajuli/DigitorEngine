#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class PluginConformanceBackend : std::uint32_t {
  d3d12,
  vulkan,
  metal,
  opengl_es,
};

enum class PluginConformanceKind : std::uint32_t {
  filter,
  effect,
  transition,
};

struct PluginBindingContract {
  std::string name;
  std::uint32_t set = 0;
  std::uint32_t binding = 0;
  std::uint32_t array_count = 1;
  bool writable = false;
};

struct PluginBackendArtifactContract {
  PluginConformanceBackend backend = PluginConformanceBackend::vulkan;
  std::string artifact_sha256;
  std::string entry_point;
  std::vector<PluginBindingContract> bindings;
  std::uint32_t push_constant_bytes = 0;
  std::uint32_t parameter_block_bytes = 0;
  bool preserves_alpha = true;
  bool deterministic = true;
};

struct PluginCrossBackendRequest {
  std::string plugin_id;
  std::string version;
  std::string package_sha256;
  PluginConformanceKind kind = PluginConformanceKind::filter;
  std::vector<PluginBackendArtifactContract> artifacts;
  bool preview_export_same_contract = true;
};

enum class PluginCrossBackendStatus : std::uint32_t {
  conformant,
  invalid_identity,
  missing_backend,
  duplicate_backend,
  invalid_artifact,
  binding_mismatch,
  parameter_layout_mismatch,
  alpha_contract_mismatch,
  determinism_mismatch,
  preview_export_contract_mismatch,
};

struct PluginCrossBackendResult {
  PluginCrossBackendStatus status = PluginCrossBackendStatus::invalid_identity;
  std::string diagnostic;
};

PluginCrossBackendResult validate_plugin_cross_backend_conformance(
    const PluginCrossBackendRequest& request);

}  // namespace digitor
