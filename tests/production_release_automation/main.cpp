#include "digitor/production_release_automation.hpp"

#include <array>
#include <string>

namespace {

digitor::ReleaseArtifactInput artifact(digitor::ReleasePlatform platform,
                                       const char* name, char hash_char) {
  digitor::ReleaseArtifactInput value;
  value.platform = platform;
  value.architecture = platform == digitor::ReleasePlatform::android ? "arm64-v8a" : "x86_64";
  value.package_name = name;
  value.sha256 = std::string(64u, hash_char);
  value.size_bytes = 1024u;
  value.sbom_attached = true;
  value.provenance_attached = true;
  value.consumer_smoke_passed = true;
  value.exported_symbols_verified = true;
  value.signing_complete = true;
  value.hardware_evidence_attached = true;
  return value;
}

}  // namespace

int main() {
  digitor::ReleaseAutomationRequest request;
  request.version = "5.0.0";
  request.tag = "v5.0.0";
  request.source_commit = "abcdef0123456789";
  request.abi_major = 5u;
  request.artifacts = {
      artifact(digitor::ReleasePlatform::windows, "digitor-windows.zip", 'a'),
      artifact(digitor::ReleasePlatform::android, "digitor-android.zip", 'b'),
      artifact(digitor::ReleasePlatform::macos, "digitor-macos.zip", 'c'),
      artifact(digitor::ReleasePlatform::ios, "digitor-ios.zip", 'd')};

  const auto ready = digitor::validate_release_automation(request);
  if (ready.status != digitor::ReleaseAutomationStatus::ready ||
      ready.digest == 0u || ready.missing_platform_mask != 0u) return 1;

  request.artifacts.back().hardware_evidence_attached = false;
  const auto blocked = digitor::validate_release_automation(request);
  if (blocked.status != digitor::ReleaseAutomationStatus::blocked ||
      blocked.blocked_artifact_count != 1u) return 2;

  request.artifacts.pop_back();
  const auto missing = digitor::validate_release_automation(request);
  if ((missing.missing_platform_mask & (1u << 3u)) == 0u) return 3;

  request.tag = "5.0.0";
  if (digitor::validate_release_automation(request).status !=
      digitor::ReleaseAutomationStatus::invalid) return 4;

  std::array<DigitorReleaseArtifactInput, 4> c_artifacts{};
  const char* names[4] = {"w.zip", "a.zip", "m.zip", "i.zip"};
  const char* hashes[4] = {
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
      "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"};
  for (std::uint32_t index = 0; index < 4u; ++index) {
    c_artifacts[index].platform = index;
    c_artifacts[index].architecture = "x86_64";
    c_artifacts[index].package_name = names[index];
    c_artifacts[index].sha256 = hashes[index];
    c_artifacts[index].size_bytes = 1u;
    c_artifacts[index].sbom_attached = 1u;
    c_artifacts[index].provenance_attached = 1u;
    c_artifacts[index].consumer_smoke_passed = 1u;
    c_artifacts[index].exported_symbols_verified = 1u;
    c_artifacts[index].signing_complete = 1u;
    c_artifacts[index].hardware_evidence_attached = 1u;
  }
  DigitorReleaseAutomationResult output{};
  if (digitor_validate_release_automation("5.0.0", "v5.0.0", "abcdef0", 5u, 0u, 1u,
                                          c_artifacts.data(), 4u, &output) != 0u ||
      output.status != 2u || output.digest == 0u) return 5;
  return 0;
}
