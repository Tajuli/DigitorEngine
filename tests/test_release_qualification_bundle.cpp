#include "digitor/release_qualification_bundle.hpp"

#include <cassert>
#include <string>

using namespace digitor;

namespace {
PlatformSourceReadiness source_platform(SourceReleasePlatform platform) {
  PlatformSourceReadiness value{};
  value.platform = platform;
  value.timeline_binding = NativeBindingKind::production_native;
  value.flutter_texture_binding = NativeBindingKind::production_native;
  value.encoder_binding = NativeBindingKind::production_native;
  value.selected_backend_matches_snapshot = true;
  value.selected_device_identity_matches = true;
  value.native_synchronization_bound = true;
  value.zero_copy_telemetry_bound = true;
  value.platform_compile_passed = true;
  value.implementation_identity = "provider";
  return value;
}

PlatformHardwareQualification hardware(SourceReleasePlatform platform,
                                       const std::string& commit) {
  PlatformHardwareQualification value{};
  value.platform = platform;
  value.evidence = QualificationEvidenceKind::physical_hardware;
  value.commit_identity = commit;
  value.device_identity = "device";
  value.renderer_backend_identity = "renderer";
  value.encoder_identity = "encoder";
  value.output_artifact_identity = "sha256:artifact";
  value.preview_presented = true;
  value.export_completed = true;
  value.output_decoded = true;
  value.golden_parity_passed = true;
  value.audio_video_sync_passed = true;
  value.vfr_passed = true;
  value.cancellation_passed = true;
  value.recovery_passed = true;
  value.long_run_passed = true;
  value.zero_cpu_readback = true;
  value.zero_cpu_staging = true;
  return value;
}

ReleaseQualificationBundle complete_bundle() {
  ReleaseQualificationBundle value{};
  value.source.commit_identity = "release-commit";
  value.source.package_version = "5.50.0";
  value.source.runtime_version = "5.50.0";
  value.source.cmake_package_version = "5.50.0";
  value.source.canonical_version_source = true;
  value.source.full_ci_green = true;
  value.source.install_consumer_test_passed = true;
  value.source.public_c_abi_test_passed = true;
  value.source.public_cpp_api_test_passed = true;
  value.source.platforms = {
      source_platform(SourceReleasePlatform::windows),
      source_platform(SourceReleasePlatform::android),
      source_platform(SourceReleasePlatform::macos),
      source_platform(SourceReleasePlatform::ios)};
  value.source.windows_vulkan.required = false;
  value.hardware = {
      hardware(SourceReleasePlatform::windows, value.source.commit_identity),
      hardware(SourceReleasePlatform::android, value.source.commit_identity),
      hardware(SourceReleasePlatform::macos, value.source.commit_identity),
      hardware(SourceReleasePlatform::ios, value.source.commit_identity)};
  value.release_artifacts_built = true;
  value.release_artifacts_signed = true;
  value.qualification_report_retained = true;
  return value;
}
}  // namespace

int main() {
  auto value = complete_bundle();
  assert(validate_release_qualification_bundle(value));

  value.hardware[3].evidence = QualificationEvidenceKind::simulator;
  assert(!validate_release_qualification_bundle(value));
  value = complete_bundle();

  value.hardware[1].commit_identity = "different-commit";
  assert(!validate_release_qualification_bundle(value));
  value = complete_bundle();

  value.hardware[0].zero_cpu_readback = false;
  assert(!validate_release_qualification_bundle(value));
  value = complete_bundle();

  value.hardware[2].platform = SourceReleasePlatform::android;
  assert(!validate_release_qualification_bundle(value));
  value = complete_bundle();

  value.release_artifacts_signed = false;
  assert(!validate_release_qualification_bundle(value));
  return 0;
}
