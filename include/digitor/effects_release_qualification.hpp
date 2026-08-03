#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class EffectsQualificationBackend : std::uint32_t {
  windows_d3d12,
  windows_vulkan,
  android_vulkan,
  android_gles,
  apple_metal,
};

enum class EffectsQualificationState : std::uint32_t {
  unqualified,
  passed,
  failed,
};

struct EffectsQualificationThresholds final {
  double max_sdr_rmse{1.0 / 255.0};
  double max_hdr_rmse{5.0e-4};
  double max_alpha_error{0.0};
  std::uint64_t required_preview_frames{300};
  std::uint64_t required_export_frames{300};
  std::uint64_t required_soak_frames{18000};
  std::uint32_t required_device_loss_cycles{3};
};

struct EffectsQualificationEvidence final {
  EffectsQualificationBackend backend{};
  std::string adapter_name;
  std::string driver_version;
  std::string shader_package_identity;
  std::string visual_stack_digest;
  std::uint64_t preview_frames{};
  std::uint64_t export_frames{};
  std::uint64_t soak_frames{};
  std::uint32_t device_loss_cycles{};
  double sdr_rmse{};
  double hdr_rmse{};
  double alpha_max_error{};
  std::uint64_t cpu_readbacks{};
  std::uint64_t cpu_reuploads{};
  std::uint64_t fallback_dispatches{};
  std::uint64_t preview_export_mismatches{};
  bool physical_adapter{};
  bool hdr_tested{};
  bool device_loss_recovered{};
};

struct EffectsQualificationReport final {
  EffectsQualificationState state{EffectsQualificationState::unqualified};
  std::vector<std::string> failures;
};

[[nodiscard]] EffectsQualificationReport qualify_effects_release(
    const EffectsQualificationEvidence& evidence,
    const EffectsQualificationThresholds& thresholds = {}) noexcept;

[[nodiscard]] const char* effects_qualification_backend_name(
    EffectsQualificationBackend backend) noexcept;
[[nodiscard]] const char* effects_qualification_state_name(
    EffectsQualificationState state) noexcept;

}  // namespace digitor
