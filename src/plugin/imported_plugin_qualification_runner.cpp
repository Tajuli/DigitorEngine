#include "digitor/imported_plugin_qualification_runner.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace digitor {

ImportedPluginQualificationRunner::ImportedPluginQualificationRunner(
    std::string plugin_id, std::string plugin_version,
    std::string package_identity, RemotePluginBackend backend,
    PluginPixelFormat format, bool physical_gpu, bool software_adapter) {
  evidence_.plugin_id = std::move(plugin_id);
  evidence_.plugin_version = std::move(plugin_version);
  evidence_.package_identity = std::move(package_identity);
  evidence_.backend = backend;
  evidence_.format = format;
  evidence_.physical_gpu = physical_gpu;
  evidence_.software_adapter = software_adapter;
}

void ImportedPluginQualificationRunner::record_frame(
    const ImportedPluginFrameEvidence& frame) noexcept {
  if (frame.preview) {
    ++evidence_.preview_frames;
    if (evidence_.preview_visual_stack_digest.empty())
      evidence_.preview_visual_stack_digest = frame.visual_stack_digest;
    else if (evidence_.preview_visual_stack_digest != frame.visual_stack_digest)
      ++evidence_.preview_export_stack_mismatches;
  } else {
    ++evidence_.export_frames;
    if (evidence_.export_visual_stack_digest.empty())
      evidence_.export_visual_stack_digest = frame.visual_stack_digest;
    else if (evidence_.export_visual_stack_digest != frame.visual_stack_digest)
      ++evidence_.preview_export_stack_mismatches;
  }

  if (frame.package_identity != evidence_.package_identity)
    ++evidence_.package_identity_mismatches;
  evidence_.compared_pixels += frame.compared_pixels;
  squared_error_sum_ += static_cast<long double>(frame.squared_error_sum);
  evidence_.max_absolute_error =
      std::max(evidence_.max_absolute_error, frame.max_absolute_error);
  evidence_.alpha_mismatches += frame.alpha_mismatches;
}

void ImportedPluginQualificationRunner::record_runtime_telemetry(
    const ImportedPluginRuntimeTelemetrySample& sample) noexcept {
  evidence_.cpu_readbacks += sample.cpu_readbacks;
  evidence_.cpu_uploads += sample.cpu_uploads;
  evidence_.fallback_dispatches += sample.fallback_dispatches;
}

void ImportedPluginQualificationRunner::record_device_loss_recovery(
    bool recovered) noexcept {
  if (recovered) ++evidence_.device_loss_cycles;
}

void ImportedPluginQualificationRunner::record_soak_frame() noexcept {
  ++evidence_.soak_frames;
}

ImportedPluginQualificationEvidence
ImportedPluginQualificationRunner::evidence() const {
  auto result = evidence_;
  if (result.compared_pixels != 0) {
    result.rmse = std::sqrt(static_cast<double>(
        squared_error_sum_ / static_cast<long double>(result.compared_pixels)));
  }
  return result;
}

ImportedPluginQualificationResult
ImportedPluginQualificationRunner::qualify() const noexcept {
  return qualify_imported_plugin_release(evidence());
}

}  // namespace digitor
