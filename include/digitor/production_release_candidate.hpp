#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class ReleaseCandidateStatus : std::uint32_t {
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

struct ReleaseArtifactEvidence {
  ReleasePlatform platform{ReleasePlatform::windows};
  std::string architecture;
  std::string package_name;
  std::string sha256;
  std::uint64_t size_bytes{0};
  bool consumer_smoke_passed{false};
  bool symbols_verified{false};
  bool signing_declared{false};
  bool hardware_qualified{false};
};

struct ReleaseCandidateManifest {
  std::string version;
  std::string source_commit;
  std::uint32_t abi_major{0};
  std::uint32_t abi_minor{0};
  bool sbom_attached{false};
  bool provenance_attached{false};
  bool preview_export_parity_required{true};
  bool silent_cpu_fallback_forbidden{true};
  std::vector<ReleaseArtifactEvidence> artifacts;
};

struct ReleaseCandidateResult {
  ReleaseCandidateStatus status{ReleaseCandidateStatus::invalid};
  std::uint32_t missing_platform_mask{0};
  std::uint32_t blocked_artifact_count{0};
  std::uint64_t digest{0};
};

ReleaseCandidateResult validate_release_candidate(
    const ReleaseCandidateManifest& manifest) noexcept;

}  // namespace digitor

extern "C" {

struct DigitorReleaseArtifactEvidence {
  std::uint32_t platform;
  const char* architecture;
  const char* package_name;
  const char* sha256;
  std::uint64_t size_bytes;
  std::uint32_t consumer_smoke_passed;
  std::uint32_t symbols_verified;
  std::uint32_t signing_declared;
  std::uint32_t hardware_qualified;
};

struct DigitorReleaseCandidateManifest {
  const char* version;
  const char* source_commit;
  std::uint32_t abi_major;
  std::uint32_t abi_minor;
  std::uint32_t sbom_attached;
  std::uint32_t provenance_attached;
  std::uint32_t preview_export_parity_required;
  std::uint32_t silent_cpu_fallback_forbidden;
  const DigitorReleaseArtifactEvidence* artifacts;
  std::uint32_t artifact_count;
};

struct DigitorReleaseCandidateResult {
  std::uint32_t status;
  std::uint32_t missing_platform_mask;
  std::uint32_t blocked_artifact_count;
  std::uint64_t digest;
};

std::uint32_t digitor_validate_release_candidate(
    const DigitorReleaseCandidateManifest* manifest,
    DigitorReleaseCandidateResult* result);

}
