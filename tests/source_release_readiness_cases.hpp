#pragma once

#include "digitor/source_release_readiness.hpp"

#include <cassert>

inline digitor::SourceReleaseReadiness qualified_source_release_fixture() {
  using namespace digitor;
  SourceReleaseReadiness value{};
  value.commit_identity = "release-commit";
  value.package_version = "5.50.0";
  value.runtime_version = "5.50.0";
  value.cmake_package_version = "5.50.0";
  value.canonical_version_source = true;
  value.full_ci_green = true;
  value.install_consumer_test_passed = true;
  value.public_c_abi_test_passed = true;
  value.public_cpp_api_test_passed = true;
  for (std::size_t i = 0; i < value.platforms.size(); ++i) {
    auto& platform = value.platforms[i];
    platform.platform = static_cast<SourceReleasePlatform>(i);
    platform.timeline_binding = NativeBindingKind::production_native;
    platform.flutter_texture_binding = NativeBindingKind::production_native;
    platform.encoder_binding = NativeBindingKind::production_native;
    platform.selected_backend_matches_snapshot = true;
    platform.selected_device_identity_matches = true;
    platform.native_synchronization_bound = true;
    platform.zero_copy_telemetry_bound = true;
    platform.platform_compile_passed = true;
    platform.implementation_identity = "native-platform-" + std::to_string(i);
  }
  value.windows_vulkan.required = true;
  value.windows_vulkan.external_memory_binding = NativeBindingKind::production_native;
  value.windows_vulkan.external_semaphore_bound = true;
  value.windows_vulkan.conversion_invoked_by_encoder_submit = true;
  value.windows_vulkan.converted_resource_validated = true;
  value.windows_vulkan.adapter_identity_matched = true;
  value.windows_vulkan.zero_cpu_readback = true;
  value.windows_vulkan.zero_cpu_staging = true;
  return value;
}

inline void run_source_release_readiness_cases() {
  using namespace digitor;
  const auto qualified = qualified_source_release_fixture();
  assert(validate_source_release_readiness(qualified));

  auto callback_only = qualified;
  callback_only.platforms[0].flutter_texture_binding =
      NativeBindingKind::callback_contract;
  assert(!validate_source_release_readiness(callback_only));

  auto missing_ci = qualified;
  missing_ci.full_ci_green = false;
  assert(!validate_source_release_readiness(missing_ci));

  auto version_mismatch = qualified;
  version_mismatch.runtime_version = "5.0.0";
  assert(!validate_source_release_readiness(version_mismatch));

  auto vulkan_not_invoked = qualified;
  vulkan_not_invoked.windows_vulkan.conversion_invoked_by_encoder_submit = false;
  assert(!validate_source_release_readiness(vulkan_not_invoked));
}
