#include "digitor/production_sdk_package.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <set>

namespace digitor {
namespace {

std::uint64_t append(std::uint64_t hash, const void* data, std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash;
}

bool semantic_version(const std::string& value) noexcept {
  if (value.empty() || std::count(value.begin(), value.end(), '.') != 2) {
    return false;
  }
  bool digit = false;
  for (char ch : value) {
    if (ch == '.') {
      if (!digit) return false;
      digit = false;
    } else if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      digit = true;
    } else {
      return false;
    }
  }
  return digit;
}

bool hexadecimal(const std::string& value, std::size_t length) noexcept {
  if (value.size() != length) return false;
  return std::all_of(value.begin(), value.end(), [](char ch) {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
  });
}

bool valid_platform(PackagePlatform value) noexcept {
  return static_cast<std::uint32_t>(value) <= static_cast<std::uint32_t>(PackagePlatform::ios);
}

bool valid_architecture(PackageArchitecture value) noexcept {
  return static_cast<std::uint32_t>(value) <= static_cast<std::uint32_t>(PackageArchitecture::armv7);
}

bool platform_architecture_compatible(PackagePlatform platform,
                                      PackageArchitecture architecture) noexcept {
  if (platform == PackagePlatform::ios || platform == PackagePlatform::macos) {
    return architecture == PackageArchitecture::arm64 ||
           (platform == PackagePlatform::macos && architecture == PackageArchitecture::x86_64);
  }
  if (platform == PackagePlatform::windows) {
    return architecture == PackageArchitecture::x86_64 ||
           architecture == PackageArchitecture::arm64;
  }
  return architecture == PackageArchitecture::arm64 ||
         architecture == PackageArchitecture::armv7 ||
         architecture == PackageArchitecture::x86_64;
}

}  // namespace

std::uint64_t sdk_package_digest(const SdkPackageManifest& manifest) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append(hash, manifest.engine_version.data(), manifest.engine_version.size());
  hash = append(hash, manifest.source_commit.data(), manifest.source_commit.size());
  hash = append(hash, &manifest.abi_major, sizeof(manifest.abi_major));
  hash = append(hash, &manifest.abi_minor, sizeof(manifest.abi_minor));
  hash = append(hash, &manifest.platform, sizeof(manifest.platform));
  hash = append(hash, &manifest.architecture, sizeof(manifest.architecture));
  for (const auto& symbol : manifest.exported_symbols) {
    hash = append(hash, symbol.data(), symbol.size());
  }
  for (const auto& artifact : manifest.artifacts) {
    hash = append(hash, artifact.relative_path.data(), artifact.relative_path.size());
    hash = append(hash, artifact.sha256.data(), artifact.sha256.size());
    hash = append(hash, &artifact.size_bytes, sizeof(artifact.size_bytes));
  }
  return hash;
}

PackageValidationResult validate_sdk_package(const SdkPackageManifest& manifest) noexcept {
  PackageValidationResult result;
  if (!semantic_version(manifest.engine_version) || manifest.source_commit.size() < 7u ||
      manifest.abi_major == 0u || !valid_platform(manifest.platform) ||
      !valid_architecture(manifest.architecture) || !manifest.release_build ||
      !manifest.debug_symbols_separate || !manifest.consumer_link_smoke_passed ||
      manifest.required_symbols.empty() || manifest.artifacts.empty()) {
    return result;
  }
  if (!platform_architecture_compatible(manifest.platform, manifest.architecture)) {
    result.status = PackageStatus::incompatible;
    return result;
  }

  const std::set<std::string> exported(manifest.exported_symbols.begin(),
                                       manifest.exported_symbols.end());
  for (const auto& symbol : manifest.required_symbols) {
    if (symbol.empty() || exported.find(symbol) == exported.end()) {
      ++result.missing_symbols;
    }
  }
  for (const auto& artifact : manifest.artifacts) {
    const bool invalid = artifact.relative_path.empty() || artifact.size_bytes == 0u ||
                         !hexadecimal(artifact.sha256, 64u);
    if (artifact.required && invalid) {
      ++result.missing_artifacts;
    }
  }
  if (result.missing_artifacts != 0u || result.missing_symbols != 0u) {
    return result;
  }
  result.status = PackageStatus::ready;
  result.digest = sdk_package_digest(manifest);
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_validate_sdk_package(
    const DigitorSdkPackageManifest* manifest,
    DigitorPackageValidationResult* output) {
  if (!manifest || !output || !manifest->engine_version ||
      !manifest->source_commit || !manifest->required_symbols ||
      !manifest->artifacts) {
    return 1u;
  }
  try {
    digitor::SdkPackageManifest native;
    native.engine_version = manifest->engine_version;
    native.source_commit = manifest->source_commit;
    native.abi_major = manifest->abi_major;
    native.abi_minor = manifest->abi_minor;
    native.platform = static_cast<digitor::PackagePlatform>(manifest->platform);
    native.architecture = static_cast<digitor::PackageArchitecture>(manifest->architecture);
    native.release_build = manifest->release_build != 0u;
    native.debug_symbols_separate = manifest->debug_symbols_separate != 0u;
    native.consumer_link_smoke_passed = manifest->consumer_link_smoke_passed != 0u;
    for (std::uint32_t i = 0; i < manifest->exported_symbol_count; ++i) {
      if (!manifest->exported_symbols || !manifest->exported_symbols[i]) return 1u;
      native.exported_symbols.emplace_back(manifest->exported_symbols[i]);
    }
    for (std::uint32_t i = 0; i < manifest->required_symbol_count; ++i) {
      if (!manifest->required_symbols[i]) return 1u;
      native.required_symbols.emplace_back(manifest->required_symbols[i]);
    }
    for (std::uint32_t i = 0; i < manifest->artifact_count; ++i) {
      const auto& input = manifest->artifacts[i];
      if (!input.relative_path || !input.sha256) return 1u;
      native.artifacts.push_back({input.relative_path, input.sha256,
                                  input.size_bytes, input.required != 0u});
    }
    const auto result = digitor::validate_sdk_package(native);
    output->status = static_cast<std::uint32_t>(result.status);
    output->missing_artifacts = result.missing_artifacts;
    output->missing_symbols = result.missing_symbols;
    output->digest = result.digest;
    return result.status == digitor::PackageStatus::ready ? 0u : 2u;
  } catch (...) {
    return 3u;
  }
}