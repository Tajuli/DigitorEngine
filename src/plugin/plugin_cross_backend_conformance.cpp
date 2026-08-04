#include "digitor/plugin_cross_backend_conformance.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <tuple>

namespace digitor {
namespace {

bool is_lower_hex_64(const std::string& value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return std::isdigit(c) != 0 || (c >= 'a' && c <= 'f');
         });
}

using BindingKey = std::tuple<std::string, std::uint32_t, std::uint32_t,
                              std::uint32_t, bool>;

std::vector<BindingKey> canonical_bindings(
    const std::vector<PluginBindingContract>& bindings) {
  std::vector<BindingKey> result;
  result.reserve(bindings.size());
  for (const auto& binding : bindings) {
    result.emplace_back(binding.name, binding.set, binding.binding,
                        binding.array_count, binding.writable);
  }
  std::sort(result.begin(), result.end());
  return result;
}

bool valid_binding_set(const std::vector<PluginBindingContract>& bindings) {
  std::set<std::pair<std::uint32_t, std::uint32_t>> slots;
  for (const auto& binding : bindings) {
    if (binding.name.empty() || binding.array_count == 0 ||
        !slots.emplace(binding.set, binding.binding).second) {
      return false;
    }
  }
  return true;
}

}  // namespace

PluginCrossBackendResult validate_plugin_cross_backend_conformance(
    const PluginCrossBackendRequest& request) {
  if (request.plugin_id.empty() || request.version.empty() ||
      !is_lower_hex_64(request.package_sha256)) {
    return {PluginCrossBackendStatus::invalid_identity,
            "plugin identity or package SHA-256 is invalid"};
  }
  if (!request.preview_export_same_contract) {
    return {PluginCrossBackendStatus::preview_export_contract_mismatch,
            "preview and export must use the same plugin contract"};
  }

  constexpr std::array<PluginConformanceBackend, 4> required = {
      PluginConformanceBackend::d3d12, PluginConformanceBackend::vulkan,
      PluginConformanceBackend::metal, PluginConformanceBackend::opengl_es};

  std::set<PluginConformanceBackend> seen;
  for (const auto& artifact : request.artifacts) {
    if (!seen.insert(artifact.backend).second) {
      return {PluginCrossBackendStatus::duplicate_backend,
              "duplicate backend artifact"};
    }
    if (!is_lower_hex_64(artifact.artifact_sha256) ||
        artifact.entry_point.empty() || artifact.parameter_block_bytes == 0 ||
        !valid_binding_set(artifact.bindings)) {
      return {PluginCrossBackendStatus::invalid_artifact,
              "backend artifact contract is invalid"};
    }
  }
  for (const auto backend : required) {
    if (seen.count(backend) == 0) {
      return {PluginCrossBackendStatus::missing_backend,
              "D3D12, Vulkan, Metal and OpenGL ES artifacts are required"};
    }
  }

  const auto& reference = request.artifacts.front();
  const auto reference_bindings = canonical_bindings(reference.bindings);
  for (const auto& artifact : request.artifacts) {
    if (canonical_bindings(artifact.bindings) != reference_bindings) {
      return {PluginCrossBackendStatus::binding_mismatch,
              "resource bindings differ across backend artifacts"};
    }
    if (artifact.parameter_block_bytes != reference.parameter_block_bytes ||
        artifact.push_constant_bytes != reference.push_constant_bytes) {
      return {PluginCrossBackendStatus::parameter_layout_mismatch,
              "parameter or push-constant layout differs across backends"};
    }
    if (artifact.preserves_alpha != reference.preserves_alpha ||
        !artifact.preserves_alpha) {
      return {PluginCrossBackendStatus::alpha_contract_mismatch,
              "all backend artifacts must preserve alpha"};
    }
    if (artifact.deterministic != reference.deterministic ||
        !artifact.deterministic) {
      return {PluginCrossBackendStatus::determinism_mismatch,
              "all backend artifacts must declare deterministic processing"};
    }
  }

  return {PluginCrossBackendStatus::conformant,
          "plugin package is cross-backend conformant"};
}

}  // namespace digitor
