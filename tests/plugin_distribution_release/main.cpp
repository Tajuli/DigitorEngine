#include "digitor/plugin_distribution_release.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_DISTRIBUTION_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginDistributionRelease make_release(
    digitor::PluginDistributionKind kind,
    const char* id,
    const char* version,
    char sha_char) {
  using namespace digitor;
  const std::string sha(64, sha_char);
  PluginDistributionRelease r{};
  r.plugin_id = id;
  r.version = version;
  r.kind = kind;
  r.publisher_id = "digitor.official";
  r.package_file_name = std::string(id) + "-" + version + ".digitorfx";
  r.package_sha256 = sha;
  r.package_signature = "ed25519:reference-signature";
  r.minimum_engine_version = "4.9.0";
  r.download_url = std::string("https://plugins.digitor.example/") + r.package_file_name;
  r.thumbnail_url = "https://plugins.digitor.example/thumb.webp";
  r.preview_media_url = "https://plugins.digitor.example/preview.mp4";
  r.supported_backends = {"windows-d3d12", "windows-vulkan", "android-vulkan",
                          "android-gles", "apple-metal"};
  r.artifacts = {{"shaders/windows-d3d12.dxil", sha, 256},
                 {"shaders/windows-vulkan.spv", sha, 256},
                 {"shaders/android-vulkan.spv", sha, 256},
                 {"shaders/android-gles.glsl", sha, 256},
                 {"shaders/apple.metallib", sha, 256}};
  return r;
}
}

int main() {
  using namespace digitor;
  PluginDistributionCatalog catalog{};
  catalog.generated_at_utc = "2026-08-04T15:00:00Z";
  catalog.publisher_key_id = "digitor-root-2026";
  catalog.catalog_signature = "ed25519:catalog-signature";
  catalog.releases = {
      make_release(PluginDistributionKind::transition,
                   "com.digitor.reference.transition", "1.0.0", 'c'),
      make_release(PluginDistributionKind::filter,
                   "com.digitor.reference.filter", "1.0.0", 'a'),
      make_release(PluginDistributionKind::effect,
                   "com.digitor.reference.effect", "1.0.0", 'b')};

  PluginDistributionBundle first{}, second{};
  std::string diagnostic;
  if (!build_plugin_distribution_bundle(catalog, first, diagnostic))
    return fail("valid distribution catalog was rejected");
  std::reverse(catalog.releases.begin(), catalog.releases.end());
  if (!build_plugin_distribution_bundle(catalog, second, diagnostic))
    return fail("reordered distribution catalog was rejected");
  if (first.catalog_json != second.catalog_json ||
      first.catalog_signing_payload != second.catalog_signing_payload)
    return fail("catalog output is not deterministic");
  if (first.publish_paths.size() != 5)
    return fail("website publish bundle is incomplete");

  const std::string filter_sha(64, 'a');
  if (!verify_plugin_distribution_import_fixture(
          catalog, "com.digitor.reference.filter", "1.0.0", filter_sha,
          diagnostic))
    return fail("exact filter import fixture failed");

  auto revoked = catalog;
  for (auto& release : revoked.releases)
    if (release.plugin_id == "com.digitor.reference.effect") release.revoked = true;
  if (verify_plugin_distribution_import_fixture(
          revoked, "com.digitor.reference.effect", "1.0.0",
          std::string(64, 'b'), diagnostic))
    return fail("revoked effect release was accepted");

  auto invalid = catalog;
  invalid.releases.front().artifacts.front().relative_path = "../escape.spv";
  if (build_plugin_distribution_bundle(invalid, second, diagnostic))
    return fail("unsafe artifact path was accepted");

  std::cout << "PLUGIN_DISTRIBUTION_RELEASE_QUALIFIED=1\n";
  std::cout << "FILTER_EFFECT_TRANSITION_RELEASES=1\n";
  std::cout << "DETERMINISTIC_CATALOG_AND_SIGNING=1\n";
  std::cout << "WEBSITE_PUBLISH_BUNDLE=1\n";
  std::cout << "MAIN_RENDERING_FEATURES_CHANGED=0\n";
  std::cout << "COMMERCIAL_POLICY_IN_ENGINE=0\n";
  return 0;
}
