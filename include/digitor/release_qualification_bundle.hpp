#pragma once

#include "digitor/source_release_readiness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace digitor {

enum class QualificationEvidenceKind : std::uint32_t {
  none = 0,
  contract_test = 1,
  compile_only = 2,
  simulator = 3,
  physical_hardware = 4,
};

struct PlatformHardwareQualification final {
  SourceReleasePlatform platform{SourceReleasePlatform::windows};
  QualificationEvidenceKind evidence{QualificationEvidenceKind::none};
  std::string commit_identity;
  std::string device_identity;
  std::string renderer_backend_identity;
  std::string encoder_identity;
  std::string output_artifact_identity;
  bool preview_presented{};
  bool export_completed{};
  bool output_decoded{};
  bool golden_parity_passed{};
  bool audio_video_sync_passed{};
  bool vfr_passed{};
  bool cancellation_passed{};
  bool recovery_passed{};
  bool long_run_passed{};
  bool zero_cpu_readback{};
  bool zero_cpu_staging{};
};

struct ReleaseQualificationBundle final {
  SourceReleaseReadiness source;
  std::array<PlatformHardwareQualification, 4> hardware{};
  bool release_artifacts_built{};
  bool release_artifacts_signed{};
  bool qualification_report_retained{};
};

struct ReleaseQualificationValidation final {
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] constexpr std::size_t qualification_platform_index(
    SourceReleasePlatform platform) noexcept {
  return static_cast<std::size_t>(platform);
}

[[nodiscard]] inline ReleaseQualificationValidation
validate_release_qualification_bundle(const ReleaseQualificationBundle& value) {
  const auto source = validate_source_release_readiness(value.source);
  if (!source) return {source.result, source.diagnostic};

  std::array<bool, 4> seen{};
  for (const auto& platform : value.hardware) {
    const auto index = qualification_platform_index(platform.platform);
    if (index >= seen.size() || seen[index])
      return {DIGITOR_RESULT_INVALID_ARGUMENT,
              "hardware qualification has duplicate or invalid platform entries"};
    seen[index] = true;

    if (platform.evidence != QualificationEvidenceKind::physical_hardware)
      return {DIGITOR_RESULT_NOT_INITIALIZED,
              "physical-hardware evidence is required for every release platform"};
    if (platform.commit_identity != value.source.commit_identity)
      return {DIGITOR_RESULT_INVALID_ARGUMENT,
              "hardware evidence must match the exact release commit"};
    if (platform.device_identity.empty() ||
        platform.renderer_backend_identity.empty() ||
        platform.encoder_identity.empty() ||
        platform.output_artifact_identity.empty())
      return {DIGITOR_RESULT_INVALID_ARGUMENT,
              "hardware device/backend/encoder/artifact identity is incomplete"};
    if (!platform.preview_presented || !platform.export_completed ||
        !platform.output_decoded || !platform.golden_parity_passed ||
        !platform.audio_video_sync_passed || !platform.vfr_passed ||
        !platform.cancellation_passed || !platform.recovery_passed ||
        !platform.long_run_passed || !platform.zero_cpu_readback ||
        !platform.zero_cpu_staging)
      return {DIGITOR_RESULT_NOT_INITIALIZED,
              "platform physical qualification evidence is incomplete"};
  }

  for (const bool present : seen)
    if (!present)
      return {DIGITOR_RESULT_INVALID_ARGUMENT,
              "Windows, Android, macOS and iOS hardware entries are required"};

  if (!value.release_artifacts_built || !value.release_artifacts_signed ||
      !value.qualification_report_retained)
    return {DIGITOR_RESULT_NOT_INITIALIZED,
            "built, signed artifacts and a retained qualification report are required"};

  return {DIGITOR_RESULT_OK, "release qualification complete"};
}

}  // namespace digitor
