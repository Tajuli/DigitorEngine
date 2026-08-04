#include "digitor/plugin_authoring_sdk.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2 || std::string(argv[1]) != "--self-test") {
    std::cerr << "usage: digitor_plugin_author --self-test\n";
    return 2;
  }
  using namespace digitor;
  const std::string sha(64, 'a');
  PluginAuthoringManifest manifest{};
  manifest.plugin_id = "com.digitor.reference.transition";
  manifest.version = "1.0.0";
  manifest.kind = PluginAuthoringKind::transition;
  manifest.publisher_id = "digitor";
  manifest.minimum_engine_version = "4.9.0";
  manifest.category = "transitions";
  manifest.tags = {"reference", "code-free"};
  manifest.localized_text = {{"en", "Reference Transition", "Two-input code-free transition."}};
  manifest.parameters = {{"softness", PluginAuthoringParameterType::number, "Softness", "Transition", 0.0, 1.0, 0.2, true, {}}};
  manifest.artifacts = {{RemotePluginBackend::windows_d3d12, "shaders/windows-d3d12.dxil", sha},
                        {RemotePluginBackend::windows_vulkan, "shaders/windows-vulkan.spv", sha},
                        {RemotePluginBackend::android_vulkan, "shaders/android-vulkan.spv", sha},
                        {RemotePluginBackend::android_gles, "shaders/android-gles.glsl", sha},
                        {RemotePluginBackend::apple_metal, "shaders/apple.metallib", sha}};
  std::vector<PluginAuthoringFile> files;
  for (const auto& artifact : manifest.artifacts) files.push_back({artifact.relative_path, sha, 128});
  files.push_back({"assets/thumbnail.webp", sha, 64});
  PluginAuthoringPackagePlan plan{};
  std::string diagnostic;
  if (!build_plugin_authoring_package_plan(manifest, files, plan, diagnostic)) {
    std::cerr << diagnostic << '\n';
    return 1;
  }
  std::cout << "PACKAGE=" << plan.package_file_name << '\n';
  std::cout << "FILES=" << plan.files.size() << '\n';
  std::cout << "SIGNING_PAYLOAD_BYTES=" << plan.signing_payload.size() << '\n';
  return 0;
}
