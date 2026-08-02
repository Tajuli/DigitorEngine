#pragma once

#include "digitor/export_render_snapshot.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class ExportEvidenceKind : std::uint32_t { contract_test = 1, compile_only = 2, simulator = 3, physical_device = 4 };
enum class ExportQualificationPlatform : std::uint32_t { windows = 1, android = 2, macos = 3, ios = 4 };

struct ExportParityEvidence final {
  bool preview_and_export_share_snapshot{};
  bool output_decoded{};
  bool golden_frames_compared{};
  double max_absolute_error{};
  double mean_absolute_error{};
  bool timestamps_match{};
  bool color_metadata_match{};
  bool alpha_policy_match{};
};

struct ExportRuntimeEvidence final {
  bool audio_video_sync_verified{};
  std::int64_t max_av_sync_error_us{};
  bool vfr_verified{};
  bool cancellation_verified{};
  bool resume_verified{};
  bool device_loss_or_codec_reset_verified{};
  bool long_run_verified{};
  std::uint64_t tested_duration_us{};
  std::uint64_t cpu_readbacks{};
  std::uint64_t cpu_staging_frames{};
};

struct ExportArtifactEvidence final {
  bool artifact_retained{};
  bool hardware_identity_retained{};
  bool codec_and_container_reported{};
  bool unsupported_combinations_reported{};
  std::string artifact_identity;
  std::string hardware_identity;
};

struct ExportPlatformQualification final {
  ExportQualificationPlatform platform{ExportQualificationPlatform::windows};
  ExportEvidenceKind evidence_kind{ExportEvidenceKind::contract_test};
  DigitorRendererBackend renderer_backend{DIGITOR_RENDERER_CPU};
  EncoderBackend encoder_backend{EncoderBackend::software};
  std::string codec;
  std::string container;
  ExportParityEvidence parity;
  ExportRuntimeEvidence runtime;
  ExportArtifactEvidence artifact;
};

struct ExportReleaseThresholds final {
  double max_absolute_error{0.002};
  double mean_absolute_error{0.0005};
  std::int64_t max_av_sync_error_us{20000};
  std::uint64_t minimum_long_run_us{30ULL * 60ULL * 1000000ULL};
};

struct ExportReleaseValidation final {
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] inline ExportReleaseValidation validate_export_platform_qualification(
    const ExportPlatformQualification& value, const ExportReleaseThresholds& limits = {}) {
  if (value.evidence_kind != ExportEvidenceKind::physical_device)
    return {DIGITOR_RESULT_UNSUPPORTED, "release qualification requires physical-device evidence"};
  if (value.renderer_backend == DIGITOR_RENDERER_CPU || value.encoder_backend == EncoderBackend::software)
    return {DIGITOR_RESULT_UNSUPPORTED, "claimed zero-copy qualification cannot use CPU/software backends"};
  if (!value.parity.preview_and_export_share_snapshot || !value.parity.output_decoded || !value.parity.golden_frames_compared)
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "decoded output and preview/export golden-frame evidence are required"};
  if (value.parity.max_absolute_error > limits.max_absolute_error || value.parity.mean_absolute_error > limits.mean_absolute_error)
    return {DIGITOR_RESULT_INTERNAL_ERROR, "preview/export parity exceeds the documented threshold"};
  if (!value.parity.timestamps_match || !value.parity.color_metadata_match || !value.parity.alpha_policy_match)
    return {DIGITOR_RESULT_INTERNAL_ERROR, "timestamp, color metadata, or alpha-policy parity failed"};
  if (!value.runtime.audio_video_sync_verified || value.runtime.max_av_sync_error_us > limits.max_av_sync_error_us)
    return {DIGITOR_RESULT_INTERNAL_ERROR, "audio/video synchronization qualification failed"};
  if (!value.runtime.vfr_verified || !value.runtime.cancellation_verified || !value.runtime.resume_verified ||
      !value.runtime.device_loss_or_codec_reset_verified || !value.runtime.long_run_verified ||
      value.runtime.tested_duration_us < limits.minimum_long_run_us)
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "VFR, cancellation, resume, reset/device-loss, and long-run evidence are required"};
  if (value.runtime.cpu_readbacks != 0 || value.runtime.cpu_staging_frames != 0)
    return {DIGITOR_RESULT_INTERNAL_ERROR, "claimed zero-copy path recorded CPU readback or staging"};
  if (!value.artifact.artifact_retained || !value.artifact.hardware_identity_retained ||
      !value.artifact.codec_and_container_reported || !value.artifact.unsupported_combinations_reported ||
      value.artifact.artifact_identity.empty() || value.artifact.hardware_identity.empty() ||
      value.codec.empty() || value.container.empty())
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "retained artifact, hardware identity, and truthful capability report are required"};
  return {DIGITOR_RESULT_OK, "qualified"};
}

[[nodiscard]] inline ExportReleaseValidation validate_export_release_matrix(
    const std::vector<ExportPlatformQualification>& matrix, const ExportReleaseThresholds& limits = {}) {
  bool windows = false, android = false, macos = false, ios = false;
  for (const auto& item : matrix) {
    const auto result = validate_export_platform_qualification(item, limits);
    if (!result) return result;
    switch (item.platform) {
      case ExportQualificationPlatform::windows: windows = true; break;
      case ExportQualificationPlatform::android: android = true; break;
      case ExportQualificationPlatform::macos: macos = true; break;
      case ExportQualificationPlatform::ios: ios = true; break;
    }
  }
  if (!windows || !android || !macos || !ios)
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "release matrix requires qualified Windows, Android, macOS, and iOS evidence"};
  return {DIGITOR_RESULT_OK, "release qualified"};
}

}  // namespace digitor
