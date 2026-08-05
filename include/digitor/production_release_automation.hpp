#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class ReleaseAutomationStatus : std::uint32_t {
  invalid = 0,
  blocked = 1,
  ready = 2,
};

enum class ReleasePlatform : std::uint32_t {
  windows = 0,
  android = 1,
  macos = 2,
  ios = 3,
};

struct ReleaseArtifactInput {
  ReleasePlatform platform = ReleasePlatform::windows;
  std::string architecture;
  std::string package_name;
  std::string sha256;
  std::uint64_t size_bytes = 0;
  bool sbom_attached = false;
  bool provenance_attached = false;
  bool consumer_smoke_passed = false;
  bool exported_symbols_verified = false;
  bool signing_complete = false;
  bool hardware_evidence_attached = false;
};

struct ReleaseAutomationRequest {
  std::string version;
  std::string tag;
  std::string source_commit;
  std::uint32_t abi_major = 0;
  std::uint32_t abi_minor = 0;
  bool dry_run = true;
  std::vector<ReleaseArtifactInput> artifacts;
};

struct ReleaseAutomationResult {
  ReleaseAutomationStatus status = ReleaseAutomationStatus::invalid;
  std::uint32_t missing_platform_mask = 0;
  std::uint32_t blocked_artifact_count = 0;
  std::uint64_t digest = 0;
};

ReleaseAutomationResult validate_release_automation(
    const ReleaseAutomationRequest& request) noexcept;

}  // namespace digitor

extern "C" {

struct DigitorReleaseArtifactInput {
  std::uint32_t platform;
  const char* architecture;
  const char* package_name;
  const char* sha256;
  std::uint64_t size_bytes;
  std::uint32_t sbom_attached;
  std::uint32_t provenance_attached;
  std::uint32_t consumer_smoke_passed;
  std::uint32_t exported_symbols_verified;
  std::uint32_t signing_complete;
  std::uint32_t hardware_evidence_attached;
};

struct DigitorReleaseAutomationResult {
  std::uint32_t status;
  std::uint32_t missing_platform_mask;
  std::uint32_t blocked_artifact_count;
  std::uint64_t digest;
};

std::uint32_t digitor_validate_release_automation(
    const char* version,
    const char* tag,
    const char* source_commit,
    std::uint32_t abi_major,
    std::uint32_t abi_minor,
    std::uint32_t dry_run,
    const DigitorReleaseArtifactInput* artifacts,
    std::uint32_t artifact_count,
    DigitorReleaseAutomationResult* output);

}
