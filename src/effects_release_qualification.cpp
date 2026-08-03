#include "digitor/effects_release_qualification.hpp"

#include <cmath>

namespace digitor {
namespace {

void require(bool condition, const char* message,
             EffectsQualificationReport& report) {
  if (!condition) report.failures.emplace_back(message);
}

}  // namespace

EffectsQualificationReport qualify_effects_release(
    const EffectsQualificationEvidence& evidence,
    const EffectsQualificationThresholds& thresholds) noexcept {
  EffectsQualificationReport report{};

  require(!evidence.adapter_name.empty(), "adapter name is missing", report);
  require(!evidence.driver_version.empty(), "driver version is missing", report);
  require(!evidence.shader_package_identity.empty(),
          "shader package identity is missing", report);
  require(!evidence.visual_stack_digest.empty(),
          "visual stack digest is missing", report);
  require(evidence.physical_adapter,
          "physical adapter evidence is required", report);
  require(evidence.preview_frames >= thresholds.required_preview_frames,
          "preview frame coverage is insufficient", report);
  require(evidence.export_frames >= thresholds.required_export_frames,
          "export frame coverage is insufficient", report);
  require(evidence.soak_frames >= thresholds.required_soak_frames,
          "soak frame coverage is insufficient", report);
  require(evidence.preview_export_mismatches == 0,
          "preview/export output mismatch detected", report);
  require(std::isfinite(evidence.sdr_rmse) &&
              evidence.sdr_rmse <= thresholds.max_sdr_rmse,
          "SDR numerical parity threshold exceeded", report);
  require(evidence.hdr_tested, "HDR qualification evidence is missing", report);
  require(std::isfinite(evidence.hdr_rmse) &&
              evidence.hdr_rmse <= thresholds.max_hdr_rmse,
          "HDR numerical parity threshold exceeded", report);
  require(std::isfinite(evidence.alpha_max_error) &&
              evidence.alpha_max_error <= thresholds.max_alpha_error,
          "alpha preservation threshold exceeded", report);
  require(evidence.cpu_readbacks == 0, "CPU readback detected", report);
  require(evidence.cpu_reuploads == 0, "CPU re-upload detected", report);
  require(evidence.fallback_dispatches == 0,
          "silent fallback dispatch detected", report);
  require(evidence.device_loss_recovered,
          "device-loss recovery evidence is missing", report);
  require(evidence.device_loss_cycles >= thresholds.required_device_loss_cycles,
          "device-loss recovery cycle coverage is insufficient", report);

  if (report.failures.empty()) {
    report.state = EffectsQualificationState::passed;
  } else if (!evidence.physical_adapter || evidence.adapter_name.empty() ||
             evidence.shader_package_identity.empty()) {
    report.state = EffectsQualificationState::unqualified;
  } else {
    report.state = EffectsQualificationState::failed;
  }
  return report;
}

const char* effects_qualification_backend_name(
    EffectsQualificationBackend backend) noexcept {
  switch (backend) {
    case EffectsQualificationBackend::windows_d3d12: return "windows-d3d12";
    case EffectsQualificationBackend::windows_vulkan: return "windows-vulkan";
    case EffectsQualificationBackend::android_vulkan: return "android-vulkan";
    case EffectsQualificationBackend::android_gles: return "android-gles";
    case EffectsQualificationBackend::apple_metal: return "apple-metal";
  }
  return "unknown";
}

const char* effects_qualification_state_name(
    EffectsQualificationState state) noexcept {
  switch (state) {
    case EffectsQualificationState::unqualified: return "UNQUALIFIED";
    case EffectsQualificationState::passed: return "PASSED";
    case EffectsQualificationState::failed: return "FAILED";
  }
  return "UNKNOWN";
}

}  // namespace digitor
