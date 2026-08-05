#include "digitor/production_sdk_package.hpp"

#include <cstdint>
#include <string>

int main() {
  using namespace digitor;
  const std::string hash(64u, 'a');
  SdkPackageManifest manifest;
  manifest.engine_version = "4.9.0";
  manifest.source_commit = "3303546becde8c3ebcce41b6df5eae948da29b0b";
  manifest.abi_major = 1u;
  manifest.platform = PackagePlatform::windows;
  manifest.architecture = PackageArchitecture::x86_64;
  manifest.consumer_link_smoke_passed = true;
  manifest.exported_symbols = {"digitor_initialize", "digitor_shutdown"};
  manifest.required_symbols = {"digitor_initialize", "digitor_shutdown"};
  manifest.artifacts = {{"include/digitor.h", hash, 1024u, true},
                        {"lib/digitor.lib", hash, 4096u, true}};
  const auto ready = validate_sdk_package(manifest);
  if (ready.status != PackageStatus::ready || ready.digest == 0u) return 1;

  auto missing_symbol = manifest;
  missing_symbol.required_symbols.push_back("digitor_missing");
  if (validate_sdk_package(missing_symbol).missing_symbols != 1u) return 2;

  auto missing_artifact = manifest;
  missing_artifact.artifacts[0].sha256 = "bad";
  if (validate_sdk_package(missing_artifact).missing_artifacts != 1u) return 3;

  auto incompatible = manifest;
  incompatible.platform = PackagePlatform::ios;
  incompatible.architecture = PackageArchitecture::x86_64;
  if (validate_sdk_package(incompatible).status != PackageStatus::incompatible) return 4;

  const char* exports[] = {"digitor_initialize", "digitor_shutdown"};
  const char* required[] = {"digitor_initialize", "digitor_shutdown"};
  DigitorPackageArtifact artifacts[] = {{"include/digitor.h", hash.c_str(), 1024u, 1u},
                                        {"lib/digitor.lib", hash.c_str(), 4096u, 1u}};
  DigitorSdkPackageManifest input{};
  input.engine_version = "4.9.0";
  input.source_commit = "3303546becde8c3ebcce41b6df5eae948da29b0b";
  input.abi_major = 1u;
  input.platform = 0u;
  input.architecture = 0u;
  input.release_build = 1u;
  input.debug_symbols_separate = 1u;
  input.consumer_link_smoke_passed = 1u;
  input.exported_symbols = exports;
  input.exported_symbol_count = 2u;
  input.required_symbols = required;
  input.required_symbol_count = 2u;
  input.artifacts = artifacts;
  input.artifact_count = 2u;
  DigitorPackageValidationResult output{};
  if (digitor_validate_sdk_package(&input, &output) != 0u || output.digest == 0u) return 5;
  return 0;
}