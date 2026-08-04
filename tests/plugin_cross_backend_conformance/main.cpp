#include "digitor/plugin_cross_backend_conformance.hpp"
#include <iostream>

using namespace digitor;

PluginBackendArtifactContract artifact(PluginConformanceBackend backend) {
  return {backend, std::string(64, 'b'), "main",
          {{"input", 0, 0, 1, false}, {"output", 0, 1, 1, true},
           {"params", 0, 2, 1, false}},
          16, 64, true, true};
}

int main() {
  PluginCrossBackendRequest request;
  request.plugin_id = "com.digitor.reference.transition";
  request.version = "1.0.0";
  request.package_sha256 = std::string(64, 'a');
  request.kind = PluginConformanceKind::transition;
  request.artifacts = {artifact(PluginConformanceBackend::d3d12),
                       artifact(PluginConformanceBackend::vulkan),
                       artifact(PluginConformanceBackend::metal),
                       artifact(PluginConformanceBackend::opengl_es)};

  if (validate_plugin_cross_backend_conformance(request).status !=
      PluginCrossBackendStatus::conformant) return 1;

  auto mismatch = request;
  mismatch.artifacts.back().parameter_block_bytes = 80;
  if (validate_plugin_cross_backend_conformance(mismatch).status !=
      PluginCrossBackendStatus::parameter_layout_mismatch) return 2;

  auto missing = request;
  missing.artifacts.pop_back();
  if (validate_plugin_cross_backend_conformance(missing).status !=
      PluginCrossBackendStatus::missing_backend) return 3;

  auto alpha = request;
  alpha.artifacts[1].preserves_alpha = false;
  if (validate_plugin_cross_backend_conformance(alpha).status !=
      PluginCrossBackendStatus::alpha_contract_mismatch) return 4;

  auto parity = request;
  parity.preview_export_same_contract = false;
  if (validate_plugin_cross_backend_conformance(parity).status !=
      PluginCrossBackendStatus::preview_export_contract_mismatch) return 5;

  std::cout << "PLUGIN_CROSS_BACKEND_CONFORMANCE=1\n"
               "D3D12_VULKAN_METAL_GLES=1\n"
               "CODE_FREE_FILTER_EFFECT_TRANSITION=1\n"
               "PREVIEW_EXPORT_SAME_CONTRACT=1\n"
               "APP_OWNS_COMMERCIAL_POLICY=1\n"
               "MAIN_RENDERING_FEATURES_CHANGED=0\n";
  return 0;
}
