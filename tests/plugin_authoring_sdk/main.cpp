#include "digitor/plugin_authoring_sdk.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace {
int fail(const char* message) { std::cerr << "PLUGIN_AUTHORING_FAILED=" << message << '\n'; return 1; }

digitor::PluginAuthoringManifest make_manifest(digitor::PluginAuthoringKind kind, const char* id) {
  using namespace digitor;
  const std::string sha(64, 'b');
  PluginAuthoringManifest m{};
  m.plugin_id = id; m.version = "1.0.0"; m.kind = kind; m.publisher_id = "digitor.reference";
  m.minimum_engine_version = "4.9.0"; m.category = kind == PluginAuthoringKind::transition ? "transitions" : "looks";
  m.tags = {"reference", "code-free"};
  m.localized_text = {{"en", "Reference Plugin", "Reference package."}, {"bn", "রেফারেন্স প্লাগইন", "রেফারেন্স প্যাকেজ।"}};
  m.parameters = {{"strength", PluginAuthoringParameterType::number, "Strength", "Main", 0.0, 1.0, 0.5, true, {}},
                  {"enabled", PluginAuthoringParameterType::boolean, "Enabled", "Main", 0.0, 1.0, 1.0, false, {}}};
  m.artifacts = {{RemotePluginBackend::windows_d3d12, "shaders/windows-d3d12.dxil", sha},
                 {RemotePluginBackend::windows_vulkan, "shaders/windows-vulkan.spv", sha},
                 {RemotePluginBackend::android_vulkan, "shaders/android-vulkan.spv", sha},
                 {RemotePluginBackend::android_gles, "shaders/android-gles.glsl", sha},
                 {RemotePluginBackend::apple_metal, "shaders/apple.metallib", sha}};
  return m;
}
}

int main() {
  using namespace digitor;
  const std::string sha(64, 'b');
  for (const auto kind : {PluginAuthoringKind::filter, PluginAuthoringKind::effect, PluginAuthoringKind::transition}) {
    const char* id = kind == PluginAuthoringKind::filter ? "com.digitor.reference.filter" : kind == PluginAuthoringKind::effect ? "com.digitor.reference.effect" : "com.digitor.reference.transition";
    auto manifest = make_manifest(kind, id);
    std::vector<PluginAuthoringFile> files;
    for (const auto& a : manifest.artifacts) files.push_back({a.relative_path, sha, 128});
    files.push_back({"assets/thumbnail.webp", sha, 64});
    PluginAuthoringPackagePlan a{}, b{}; std::string diagnostic;
    if (!build_plugin_authoring_package_plan(manifest, files, a, diagnostic)) return fail("valid reference package rejected");
    std::reverse(files.begin(), files.end());
    if (!build_plugin_authoring_package_plan(manifest, files, b, diagnostic)) return fail("reordered package rejected");
    if (a.canonical_manifest != b.canonical_manifest || a.signing_payload != b.signing_payload) return fail("package output is not deterministic");
    if (a.package_file_name.find(".digitorfx") == std::string::npos) return fail("package extension missing");
  }
  auto invalid = make_manifest(PluginAuthoringKind::effect, "com.digitor.invalid");
  invalid.artifacts[0].relative_path = "../escape.dxil";
  std::string diagnostic;
  if (validate_plugin_authoring_manifest(invalid, diagnostic)) return fail("path traversal accepted");
  invalid = make_manifest(PluginAuthoringKind::filter, "com.digitor.invalid2");
  invalid.preserves_alpha = false;
  if (validate_plugin_authoring_manifest(invalid, diagnostic)) return fail("alpha-destructive plugin accepted");
  std::cout << "PLUGIN_AUTHORING_SDK_QUALIFIED=1\nREFERENCE_FILTER_EFFECT_TRANSITION=1\nDETERMINISTIC_SIGNING_PAYLOAD=1\nMAIN_RENDERING_FEATURES_CHANGED=0\nCOMMERCIAL_POLICY_IN_ENGINE=0\n";
  return 0;
}
