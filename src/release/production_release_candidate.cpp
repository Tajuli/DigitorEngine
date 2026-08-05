#include "digitor/production_release_candidate.hpp"

#include <array>
#include <cstddef>
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

bool valid_version(const std::string& value) noexcept {
  if (value.empty()) {
    return false;
  }
  std::uint32_t dots = 0;
  for (const char ch : value) {
    if (ch == '.') {
      ++dots;
    } else if (ch < '0' || ch > '9') {
      return false;
    }
  }
  return dots == 2u;
}

bool valid_sha256(const std::string& value) noexcept {
  if (value.size() != 64u) {
    return false;
  }
  for (const char ch : value) {
    const bool decimal = ch >= '0' && ch <= '9';
    const bool lower_hex = ch >= 'a' && ch <= 'f';
    if (!decimal && !lower_hex) {
      return false;
    }
  }
  return true;
}

bool valid_platform(ReleasePlatform platform) noexcept {
  return static_cast<std::uint32_t>(platform) <=
         static_cast<std::uint32_t>(ReleasePlatform::ios);
}

}  // namespace

ReleaseCandidateResult validate_release_candidate(
    const ReleaseCandidateManifest& manifest) noexcept {
  ReleaseCandidateResult result;
  if (!valid_version(manifest.version) || manifest.source_commit.size() < 7u ||
      manifest.abi_major == 0u || !manifest.sbom_attached ||
      !manifest.provenance_attached ||
      !manifest.preview_export_parity_required ||
      !manifest.silent_cpu_fallback_forbidden || manifest.artifacts.empty()) {
    return result;
  }

  std::array<bool, 4> platform_seen{false, false, false, false};
  std::set<std::string> package_names;
  std::set<std::string> hashes;
  std::uint64_t digest = 1469598103934665603ull;
  digest = append(digest, manifest.version.data(), manifest.version.size());
  digest = append(digest, manifest.source_commit.data(),
                  manifest.source_commit.size());
  digest = append(digest, &manifest.abi_major, sizeof(manifest.abi_major));
  digest = append(digest, &manifest.abi_minor, sizeof(manifest.abi_minor));

  for (const auto& artifact : manifest.artifacts) {
    if (!valid_platform(artifact.platform) || artifact.architecture.empty() ||
        artifact.package_name.empty() || !valid_sha256(artifact.sha256) ||
        artifact.size_bytes == 0u ||
        !package_names.insert(artifact.package_name).second ||
        !hashes.insert(artifact.sha256).second) {
      return result;
    }
    const auto platform_index = static_cast<std::size_t>(artifact.platform);
    platform_seen[platform_index] = true;
    if (!artifact.consumer_smoke_passed || !artifact.symbols_verified ||
        !artifact.signing_declared || !artifact.hardware_qualified) {
      ++result.blocked_artifact_count;
    }
    digest = append(digest, &artifact.platform, sizeof(artifact.platform));
    digest = append(digest, artifact.architecture.data(),
                    artifact.architecture.size());
    digest = append(digest, artifact.package_name.data(),
                    artifact.package_name.size());
    digest = append(digest, artifact.sha256.data(), artifact.sha256.size());
    digest = append(digest, &artifact.size_bytes, sizeof(artifact.size_bytes));
  }

  for (std::size_t index = 0; index < platform_seen.size(); ++index) {
    if (!platform_seen[index]) {
      result.missing_platform_mask |= 1u << index;
    }
  }

  result.digest = digest;
  result.status = result.missing_platform_mask == 0u &&
                          result.blocked_artifact_count == 0u
                      ? ReleaseCandidateStatus::ready
                      : ReleaseCandidateStatus::blocked;
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_validate_release_candidate(
    const DigitorReleaseCandidateManifest* manifest,
    DigitorReleaseCandidateResult* result) {
  if (!manifest || !result || !manifest->version ||
      !manifest->source_commit || !manifest->artifacts ||
      manifest->artifact_count == 0u) {
    return 1u;
  }
  try {
    digitor::ReleaseCandidateManifest native;
    native.version = manifest->version;
    native.source_commit = manifest->source_commit;
    native.abi_major = manifest->abi_major;
    native.abi_minor = manifest->abi_minor;
    native.sbom_attached = manifest->sbom_attached != 0u;
    native.provenance_attached = manifest->provenance_attached != 0u;
    native.preview_export_parity_required =
        manifest->preview_export_parity_required != 0u;
    native.silent_cpu_fallback_forbidden =
        manifest->silent_cpu_fallback_forbidden != 0u;
    native.artifacts.reserve(manifest->artifact_count);
    for (std::uint32_t index = 0; index < manifest->artifact_count; ++index) {
      const auto& source = manifest->artifacts[index];
      if (!source.architecture || !source.package_name || !source.sha256) {
        return 1u;
      }
      digitor::ReleaseArtifactEvidence artifact;
      artifact.platform =
          static_cast<digitor::ReleasePlatform>(source.platform);
      artifact.architecture = source.architecture;
      artifact.package_name = source.package_name;
      artifact.sha256 = source.sha256;
      artifact.size_bytes = source.size_bytes;
      artifact.consumer_smoke_passed = source.consumer_smoke_passed != 0u;
      artifact.symbols_verified = source.symbols_verified != 0u;
      artifact.signing_declared = source.signing_declared != 0u;
      artifact.hardware_qualified = source.hardware_qualified != 0u;
      native.artifacts.push_back(artifact);
    }
    const auto native_result = digitor::validate_release_candidate(native);
    result->status = static_cast<std::uint32_t>(native_result.status);
    result->missing_platform_mask = native_result.missing_platform_mask;
    result->blocked_artifact_count = native_result.blocked_artifact_count;
    result->digest = native_result.digest;
    return native_result.status == digitor::ReleaseCandidateStatus::invalid ? 2u
                                                                            : 0u;
  } catch (...) {
    return 3u;
  }
}
