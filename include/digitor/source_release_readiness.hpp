#pragma once

#include "digitor/digitor.h"

#include <array>
#include <cstdint>
#include <string>

namespace digitor {

enum class SourceReleasePlatform : std::uint32_t {
  windows = 0,
  android = 1,
  macos = 2,
  ios = 3,
};

enum class NativeBindingKind : std::uint32_t {
  callback_contract = 1,
  compile_only = 2,
  simulator = 3,
  production_native = 4,
};

struct PlatformSourceReadiness final {
  SourceReleasePlatform platform{SourceReleasePlatform::windows};
  NativeBindingKind timeline_binding{NativeBindingKind::callback_contract};
  NativeBindingKind flutter_texture_binding{NativeBindingKind::callback_contract};
  NativeBindingKind encoder_binding{NativeBindingKind::callback_contract};
  bool selected_backend_matches_snapshot{};
  bool selected_device_identity_matches{};
  bool native_synchronization_bound{};
  bool zero_copy_telemetry_bound{};
  bool platform_compile_passed{};
  std::string implementation_identity;
};

struct WindowsVulkanSourceReadiness final {
  bool required{};
  NativeBindingKind external_memory_binding{NativeBindingKind::callback_contract};
  bool external_semaphore_bound{};
  bool conversion_invoked_by_encoder_submit{};
  bool converted_resource_validated{};
  bool adapter_identity_matched{};
  bool zero_cpu_readback{};
  bool zero_cpu_staging{};
};

struct SourceReleaseReadiness final {
  std::string commit_identity;
  std::string package_version;
  std::string runtime_version;
  std::string cmake_package_version;
  bool canonical_version_source{};
  bool full_ci_green{};
  bool install_consumer_test_passed{};
  bool public_c_abi_test_passed{};
  bool public_cpp_api_test_passed{};
  std::array<PlatformSourceReadiness, 4> platforms{};
  WindowsVulkanSourceReadiness windows_vulkan{};
};

struct SourceReleaseValidation final {
  DigitorResult result{DIGITOR_RESULT_INVALID_ARGUMENT};
  std::string diagnostic;
  explicit operator bool() const noexcept { return result == DIGITOR_RESULT_OK; }
};

[[nodiscard]] inline SourceReleaseValidation validate_source_release_readiness(
    const SourceReleaseReadiness& value) {
  if (value.commit_identity.empty())
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "release commit identity is required"};
  if (!value.canonical_version_source || value.package_version.empty() ||
      value.package_version != value.runtime_version ||
      value.package_version != value.cmake_package_version)
    return {DIGITOR_RESULT_INVALID_ARGUMENT, "package/runtime/CMake versions are inconsistent"};
  if (!value.full_ci_green)
    return {DIGITOR_RESULT_NOT_INITIALIZED, "full required CI has not passed for the release commit"};
  if (!value.install_consumer_test_passed || !value.public_c_abi_test_passed ||
      !value.public_cpp_api_test_passed)
    return {DIGITOR_RESULT_NOT_INITIALIZED, "install consumer and public API tests are required"};

  std::array<bool, 4> seen{};
  for (const auto& platform : value.platforms) {
    const auto index = static_cast<std::size_t>(platform.platform);
    if (index >= seen.size() || seen[index])
      return {DIGITOR_RESULT_INVALID_ARGUMENT, "release matrix has duplicate or invalid platform entries"};
    seen[index] = true;
    if (platform.timeline_binding != NativeBindingKind::production_native ||
        platform.flutter_texture_binding != NativeBindingKind::production_native ||
        platform.encoder_binding != NativeBindingKind::production_native)
      return {DIGITOR_RESULT_BACKEND_UNAVAILABLE, "callback/compile/simulator bindings are not production source completion"};
    if (!platform.selected_backend_matches_snapshot ||
        !platform.selected_device_identity_matches ||
        !platform.native_synchronization_bound ||
        !platform.zero_copy_telemetry_bound ||
        !platform.platform_compile_passed ||
        platform.implementation_identity.empty())
      return {DIGITOR_RESULT_INVALID_ARGUMENT, "platform native source evidence is incomplete"};
  }
  for (const bool present : seen)
    if (!present)
      return {DIGITOR_RESULT_INVALID_ARGUMENT, "Windows, Android, macOS and iOS source entries are required"};

  if (value.windows_vulkan.required) {
    const auto& vk = value.windows_vulkan;
    if (vk.external_memory_binding != NativeBindingKind::production_native ||
        !vk.external_semaphore_bound || !vk.conversion_invoked_by_encoder_submit ||
        !vk.converted_resource_validated || !vk.adapter_identity_matched ||
        !vk.zero_cpu_readback || !vk.zero_cpu_staging)
      return {DIGITOR_RESULT_BACKEND_UNAVAILABLE, "Windows Vulkan end-to-end encoder interop is not production bound"};
  }
  return {DIGITOR_RESULT_OK, "source release ready"};
}

}  // namespace digitor
