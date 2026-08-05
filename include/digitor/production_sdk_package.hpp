#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class PackagePlatform : std::uint32_t { windows = 0, android = 1, macos = 2, ios = 3 };
enum class PackageArchitecture : std::uint32_t { x86_64 = 0, arm64 = 1, armv7 = 2 };
enum class PackageStatus : std::uint32_t { invalid = 0, ready = 1, incompatible = 2 };

struct PackageArtifact {
  std::string relative_path;
  std::string sha256;
  std::uint64_t size_bytes = 0;
  bool required = true;
};

struct SdkPackageManifest {
  std::string engine_version;
  std::string source_commit;
  std::uint32_t abi_major = 0;
  std::uint32_t abi_minor = 0;
  PackagePlatform platform = PackagePlatform::windows;
  PackageArchitecture architecture = PackageArchitecture::x86_64;
  bool release_build = true;
  bool debug_symbols_separate = true;
  bool consumer_link_smoke_passed = false;
  std::vector<std::string> exported_symbols;
  std::vector<std::string> required_symbols;
  std::vector<PackageArtifact> artifacts;
};

struct PackageValidationResult {
  PackageStatus status = PackageStatus::invalid;
  std::uint32_t missing_artifacts = 0;
  std::uint32_t missing_symbols = 0;
  std::uint64_t digest = 0;
};

PackageValidationResult validate_sdk_package(const SdkPackageManifest& manifest) noexcept;
std::uint64_t sdk_package_digest(const SdkPackageManifest& manifest) noexcept;

}  // namespace digitor

extern "C" {

struct DigitorPackageArtifact {
  const char* relative_path;
  const char* sha256;
  std::uint64_t size_bytes;
  std::uint32_t required;
};

struct DigitorSdkPackageManifest {
  const char* engine_version;
  const char* source_commit;
  std::uint32_t abi_major;
  std::uint32_t abi_minor;
  std::uint32_t platform;
  std::uint32_t architecture;
  std::uint32_t release_build;
  std::uint32_t debug_symbols_separate;
  std::uint32_t consumer_link_smoke_passed;
  const char* const* exported_symbols;
  std::uint32_t exported_symbol_count;
  const char* const* required_symbols;
  std::uint32_t required_symbol_count;
  const DigitorPackageArtifact* artifacts;
  std::uint32_t artifact_count;
};

struct DigitorPackageValidationResult {
  std::uint32_t status;
  std::uint32_t missing_artifacts;
  std::uint32_t missing_symbols;
  std::uint64_t digest;
};

std::uint32_t digitor_validate_sdk_package(const DigitorSdkPackageManifest* manifest,
                                           DigitorPackageValidationResult* output);

}