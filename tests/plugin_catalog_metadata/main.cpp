#include "digitor/plugin_catalog_metadata.hpp"

#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_CATALOG_METADATA_FAILED=" << message << '\n';
  return 1;
}
}

int main() {
  using namespace digitor;

  PluginCatalogMetadata metadata{};
  metadata.presentation.category = "transitions";
  metadata.presentation.tags = {"cinematic", "smooth"};
  metadata.presentation.thumbnail_url = "https://plugins.example/thumb.webp";
  metadata.presentation.preview_media_url = "https://plugins.example/preview.mp4";
  metadata.presentation.localized_text = {
      {"en", "Cinematic Wipe", "A smooth GPU transition."},
      {"bn", "সিনেম্যাটিক ওয়াইপ", "একটি মসৃণ GPU ট্রানজিশন।"}};
  metadata.requirements.minimum_engine_version = "4.9.0";
  metadata.requirements.supported_backends = {
      RemotePluginBackend::windows_d3d12,
      RemotePluginBackend::windows_vulkan,
      RemotePluginBackend::android_vulkan,
      RemotePluginBackend::apple_metal};
  metadata.requirements.supported_pixel_formats = {
      PluginPixelFormat::rgba16_float,
      PluginPixelFormat::rgba8_unorm};
  metadata.requirements.requires_compute = true;
  metadata.requirements.requires_fp16 = true;

  std::string diagnostic;
  if (!validate_plugin_catalog_metadata(metadata, diagnostic))
    return fail("valid metadata was rejected");

  PluginHostCapabilities host{};
  host.engine_version = "4.9.0";
  host.backend = RemotePluginBackend::windows_d3d12;
  host.pixel_format = PluginPixelFormat::rgba16_float;
  host.supports_compute = true;
  host.supports_fp16 = true;

  if (evaluate_plugin_catalog_compatibility(metadata, host, diagnostic) !=
      PluginCatalogCompatibility::compatible)
    return fail("compatible host was rejected");

  host.engine_version = "4.8.9";
  if (evaluate_plugin_catalog_compatibility(metadata, host, diagnostic) !=
      PluginCatalogCompatibility::engine_too_old)
    return fail("minimum engine version was not enforced");

  host.engine_version = "4.9.0";
  host.supports_fp16 = false;
  if (evaluate_plugin_catalog_compatibility(metadata, host, diagnostic) !=
      PluginCatalogCompatibility::fp16_required)
    return fail("FP16 requirement was not enforced");

  host.supports_fp16 = true;
  host.backend = RemotePluginBackend::android_opengl_es;
  if (evaluate_plugin_catalog_compatibility(metadata, host, diagnostic) !=
      PluginCatalogCompatibility::backend_unavailable)
    return fail("backend requirement was not enforced");

  const auto* bn = select_plugin_localized_text(metadata, "bn");
  if (!bn || bn->locale != "bn")
    return fail("requested localization was not selected");
  const auto* fallback = select_plugin_localized_text(metadata, "fr");
  if (!fallback || fallback->locale != "en")
    return fail("English fallback was not selected");

  auto invalid = metadata;
  invalid.presentation.tags.push_back("cinematic");
  if (validate_plugin_catalog_metadata(invalid, diagnostic))
    return fail("duplicate tags were accepted");

  std::cout << "PLUGIN_CATALOG_METADATA_QUALIFIED=1\n";
  std::cout << "MAIN_RENDERING_FEATURES_CHANGED=0\n";
  std::cout << "ENGINE_PROCESSING_POLICY_CHANGED=0\n";
  return 0;
}
