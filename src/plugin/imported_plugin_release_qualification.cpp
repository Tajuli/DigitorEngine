#include "digitor/imported_plugin_release_qualification.hpp"

#include <utility>

namespace digitor {
namespace {

ImportedPluginQualificationResult unqualified(std::string message) {
  return {ImportedPluginQualificationState::unqualified, std::move(message)};
}

ImportedPluginQualificationResult failed(std::string message) {
  return {ImportedPluginQualificationState::failed, std::move(message)};
}

}  // namespace

ImportedPluginQualificationResult qualify_imported_plugin_release(
    const ImportedPluginQualificationEvidence& evidence) noexcept {
  if (evidence.plugin_id.empty() || evidence.plugin_version.empty() ||
      evidence.package_identity.empty() ||
      evidence.preview_visual_stack_digest.empty() ||
      evidence.export_visual_stack_digest.empty()) {
    return unqualified("imported plugin identity or visual-stack evidence is missing");
  }
  if (!evidence.physical_gpu || evidence.software_adapter) {
    return unqualified("physical GPU evidence is required; software adapters cannot qualify");
  }
  if (evidence.preview_frames < 300 || evidence.export_frames < 300 ||
      evidence.compared_pixels == 0 || evidence.soak_frames < 18000 ||
      evidence.device_loss_cycles < 3) {
    return unqualified("minimum frame, pixel, soak, or device-loss evidence is incomplete");
  }
  if (evidence.preview_visual_stack_digest !=
          evidence.export_visual_stack_digest ||
      evidence.preview_export_stack_mismatches != 0 ||
      evidence.package_identity_mismatches != 0) {
    return failed("preview/export plugin package or visual-stack identity mismatch");
  }
  if (evidence.cpu_readbacks != 0 || evidence.cpu_uploads != 0 ||
      evidence.fallback_dispatches != 0) {
    return failed("imported plugin execution violated zero-copy or selected-backend policy");
  }
  if (evidence.alpha_mismatches != 0) {
    return failed("imported plugin alpha preservation mismatch");
  }

  const bool hdr = evidence.format == PluginPixelFormat::rgba16_float ||
                   evidence.format == PluginPixelFormat::rgba32_float;
  const double rmse_limit = hdr ? 0.0005 : (1.0 / 255.0);
  const double max_error_limit = hdr ? 0.002 : (2.0 / 255.0);
  if (evidence.rmse > rmse_limit ||
      evidence.max_absolute_error > max_error_limit) {
    return failed("imported plugin preview/export per-pixel tolerance exceeded");
  }

  return {ImportedPluginQualificationState::passed, {}};
}

}  // namespace digitor
