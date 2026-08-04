#pragma once

#include "digitor/remote_plugin_marketplace.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

enum class PluginCatalogCompatibility : std::uint32_t {
  compatible,
  engine_too_old,
  backend_unavailable,
  fp16_required,
  compute_required,
  unsupported_pixel_format,
  invalid_metadata
};

struct PluginLocalizedText final {
  std::string locale;
  std::string name;
  std::string description;
};

struct PluginCatalogPresentation final {
  std::string category;
  std::vector<std::string> tags;
  std::string thumbnail_url;
  std::string preview_media_url;
  std::vector<PluginLocalizedText> localized_text;
};

struct PluginCatalogRequirements final {
  std::string minimum_engine_version;
  std::vector<RemotePluginBackend> supported_backends;
  std::vector<PluginPixelFormat> supported_pixel_formats;
  bool requires_compute{true};
  bool requires_fp16{false};
};

struct PluginHostCapabilities final {
  std::string engine_version;
  RemotePluginBackend backend{RemotePluginBackend::windows_d3d12};
  PluginPixelFormat pixel_format{PluginPixelFormat::rgba16_float};
  bool supports_compute{true};
  bool supports_fp16{true};
};

struct PluginCatalogMetadata final {
  PluginCatalogPresentation presentation;
  PluginCatalogRequirements requirements;
};

[[nodiscard]] bool validate_plugin_catalog_metadata(
    const PluginCatalogMetadata& metadata,
    std::string& diagnostic) noexcept;

[[nodiscard]] PluginCatalogCompatibility evaluate_plugin_catalog_compatibility(
    const PluginCatalogMetadata& metadata,
    const PluginHostCapabilities& host,
    std::string& diagnostic) noexcept;

[[nodiscard]] const PluginLocalizedText* select_plugin_localized_text(
    const PluginCatalogMetadata& metadata,
    std::string_view preferred_locale) noexcept;

}  // namespace digitor
