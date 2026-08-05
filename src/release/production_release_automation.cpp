#include "digitor/production_release_automation.hpp"

#include <array>
#include <cctype>
#include <set>

namespace digitor {
namespace {

std::uint64_t append(std::uint64_t hash, const void* data,
                     std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash;
}

bool valid_version(const std::string& version) noexcept {
  if (version.empty()) return false;
  std::uint32_t dots = 0;
  bool digit = false;
  for (char value : version) {
    if (value == '.') {
      if (!digit) return false;
      digit = false;
      ++dots;
    } else if (std::isdigit(static_cast<unsigned char>(value)) != 0) {
      digit = true;
    } else {
      return false;
    }
  }
  return dots == 2u && digit;
}

bool valid_sha256(const std::string& value) noexcept {
  if (value.size() != 64u) return false;
  for (char item : value) {
    if (std::isxdigit(static_cast<unsigned char>(item)) == 0) return false;
  }
  return true;
}

bool valid_platform(ReleasePlatform platform) noexcept {
  return static_cast<std::uint32_t>(platform) <= 3u;
}

}  // namespace

ReleaseAutomationResult validate_release_automation(
    const ReleaseAutomationRequest& request) noexcept {
  ReleaseAutomationResult result;
  if (!valid_version(request.version) || request.tag != "v" + request.version ||
      request.source_commit.size() < 7u || request.abi_major == 0u ||
      request.artifacts.empty()) {
    return result;
  }

  std::array<bool, 4> present{};
  std::set<std::string> package_names;
  std::set<std::string> hashes;
  std::uint64_t digest = 1469598103934665603ull;
  digest = append(digest, request.version.data(), request.version.size());
  digest = append(digest, request.source_commit.data(), request.source_commit.size());
  digest = append(digest, &request.abi_major, sizeof(request.abi_major));
  digest = append(digest, &request.abi_minor, sizeof(request.abi_minor));

  for (const auto& artifact : request.artifacts) {
    if (!valid_platform(artifact.platform) || artifact.architecture.empty() ||
        artifact.package_name.empty() || !valid_sha256(artifact.sha256) ||
        artifact.size_bytes == 0u ||
        !package_names.insert(artifact.package_name).second ||
        !hashes.insert(artifact.sha256).second) {
      return result;
    }
    present[static_cast<std::size_t>(artifact.platform)] = true;
    const bool gated = artifact.sbom_attached && artifact.provenance_attached &&
                       artifact.consumer_smoke_passed &&
                       artifact.exported_symbols_verified &&
                       artifact.signing_complete &&
                       artifact.hardware_evidence_attached;
    if (!gated) ++result.blocked_artifact_count;
    digest = append(digest, artifact.package_name.data(), artifact.package_name.size());
    digest = append(digest, artifact.sha256.data(), artifact.sha256.size());
    digest = append(digest, &artifact.size_bytes, sizeof(artifact.size_bytes));
  }

  for (std::size_t index = 0; index < present.size(); ++index) {
    if (!present[index]) result.missing_platform_mask |= (1u << index);
  }
  result.digest = digest;
  result.status = result.missing_platform_mask == 0u &&
                          result.blocked_artifact_count == 0u
                      ? ReleaseAutomationStatus::ready
                      : ReleaseAutomationStatus::blocked;
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_validate_release_automation(
    const char* version, const char* tag, const char* source_commit,
    std::uint32_t abi_major, std::uint32_t abi_minor, std::uint32_t dry_run,
    const DigitorReleaseArtifactInput* artifacts, std::uint32_t artifact_count,
    DigitorReleaseAutomationResult* output) {
  if (!version || !tag || !source_commit || !artifacts || artifact_count == 0u ||
      !output) {
    return 1u;
  }
  try {
    digitor::ReleaseAutomationRequest request;
    request.version = version;
    request.tag = tag;
    request.source_commit = source_commit;
    request.abi_major = abi_major;
    request.abi_minor = abi_minor;
    request.dry_run = dry_run != 0u;
    request.artifacts.reserve(artifact_count);
    for (std::uint32_t index = 0; index < artifact_count; ++index) {
      const auto& input = artifacts[index];
      if (!input.architecture || !input.package_name || !input.sha256) return 1u;
      digitor::ReleaseArtifactInput artifact;
      artifact.platform = static_cast<digitor::ReleasePlatform>(input.platform);
      artifact.architecture = input.architecture;
      artifact.package_name = input.package_name;
      artifact.sha256 = input.sha256;
      artifact.size_bytes = input.size_bytes;
      artifact.sbom_attached = input.sbom_attached != 0u;
      artifact.provenance_attached = input.provenance_attached != 0u;
      artifact.consumer_smoke_passed = input.consumer_smoke_passed != 0u;
      artifact.exported_symbols_verified = input.exported_symbols_verified != 0u;
      artifact.signing_complete = input.signing_complete != 0u;
      artifact.hardware_evidence_attached = input.hardware_evidence_attached != 0u;
      request.artifacts.push_back(artifact);
    }
    const auto result = digitor::validate_release_automation(request);
    output->status = static_cast<std::uint32_t>(result.status);
    output->missing_platform_mask = result.missing_platform_mask;
    output->blocked_artifact_count = result.blocked_artifact_count;
    output->digest = result.digest;
    return 0u;
  } catch (...) {
    return 2u;
  }
}
