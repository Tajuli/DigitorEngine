#include "digitor/production_release_candidate.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

digitor::ReleaseArtifactEvidence artifact(
    digitor::ReleasePlatform platform, const char* arch,
    const char* name, char hash_digit) {
  digitor::ReleaseArtifactEvidence value;
  value.platform = platform;
  value.architecture = arch;
  value.package_name = name;
  value.sha256 = std::string(64u, hash_digit);
  value.size_bytes = 4096u;
  value.consumer_smoke_passed = true;
  value.symbols_verified = true;
  value.signing_declared = true;
  value.hardware_qualified = true;
  return value;
}

}  // namespace

int main() {
  using namespace digitor;
  ReleaseCandidateManifest manifest;
  manifest.version = "5.0.0";
  manifest.source_commit = "abcdef1234567890";
  manifest.abi_major = 5u;
  manifest.abi_minor = 0u;
  manifest.sbom_attached = true;
  manifest.provenance_attached = true;
  manifest.artifacts = {
      artifact(ReleasePlatform::windows, "x86_64", "windows-x64.zip", 'a'),
      artifact(ReleasePlatform::android, "arm64-v8a", "android-arm64.zip", 'b'),
      artifact(ReleasePlatform::macos, "universal2", "macos.xcframework.zip", 'c'),
      artifact(ReleasePlatform::ios, "arm64", "ios.xcframework.zip", 'd'),
  };

  const auto ready = validate_release_candidate(manifest);
  if (ready.status != ReleaseCandidateStatus::ready || ready.digest == 0u ||
      ready.missing_platform_mask != 0u || ready.blocked_artifact_count != 0u) {
    return 1;
  }

  manifest.artifacts.back().hardware_qualified = false;
  const auto blocked = validate_release_candidate(manifest);
  if (blocked.status != ReleaseCandidateStatus::blocked ||
      blocked.blocked_artifact_count != 1u) {
    return 2;
  }

  manifest.artifacts.pop_back();
  const auto missing = validate_release_candidate(manifest);
  if (missing.status != ReleaseCandidateStatus::blocked ||
      (missing.missing_platform_mask & (1u << 3u)) == 0u) {
    return 3;
  }

  manifest.artifacts.push_back(
      artifact(ReleasePlatform::ios, "arm64", "ios.xcframework.zip", 'a'));
  if (validate_release_candidate(manifest).status !=
      ReleaseCandidateStatus::invalid) {
    return 4;
  }

  const DigitorReleaseArtifactEvidence c_artifacts[4] = {
      {0u, "x86_64", "windows-x64.zip",
       "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
       4096u, 1u, 1u, 1u, 1u},
      {1u, "arm64-v8a", "android-arm64.zip",
       "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
       4096u, 1u, 1u, 1u, 1u},
      {2u, "universal2", "macos.xcframework.zip",
       "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
       4096u, 1u, 1u, 1u, 1u},
      {3u, "arm64", "ios.xcframework.zip",
       "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
       4096u, 1u, 1u, 1u, 1u},
  };
  const DigitorReleaseCandidateManifest c_manifest = {
      "5.0.0", "abcdef1234567890", 5u, 0u, 1u, 1u, 1u, 1u,
      c_artifacts, 4u};
  DigitorReleaseCandidateResult c_result{};
  if (digitor_validate_release_candidate(&c_manifest, &c_result) != 0u ||
      c_result.status != 2u || c_result.digest == 0u) {
    return 5;
  }
  return 0;
}
