#include "digitor/plugin_catalog_metadata.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <set>
#include <tuple>

namespace digitor {
namespace {

bool valid_token(std::string_view value, std::size_t limit) noexcept {
  if (value.empty() || value.size() > limit) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '_' || c == '-';
  });
}

bool secure_url(std::string_view value) noexcept {
  return value.empty() || (value.size() <= 2048 && value.starts_with("https://"));
}

bool parse_version(std::string_view value, std::tuple<int, int, int>& out) noexcept {
  int parts[3]{};
  std::size_t start = 0;
  for (int i = 0; i < 3; ++i) {
    const auto end = value.find('.', start);
    const auto token = value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
    if (token.empty()) return false;
    const auto result = std::from_chars(token.data(), token.data() + token.size(), parts[i]);
    if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || parts[i] < 0) return false;
    if (i < 2 && end == std::string_view::npos) return false;
    start = end == std::string_view::npos ? value.size() : end + 1;
  }
  if (start < value.size()) return false;
  out = {parts[0], parts[1], parts[2]};
  return true;
}

}  // namespace

bool validate_plugin_catalog_metadata(const PluginCatalogMetadata& metadata,
                                      std::string& diagnostic) noexcept {
  const auto& p = metadata.presentation;
  const auto& r = metadata.requirements;
  if (!valid_token(p.category, 80) || p.tags.size() > 32 ||
      !secure_url(p.thumbnail_url) || !secure_url(p.preview_media_url) ||
      p.localized_text.empty() || p.localized_text.size() > 32 ||
      r.supported_backends.empty() || r.supported_pixel_formats.empty()) {
    diagnostic = "plugin catalog metadata is incomplete or exceeds limits";
    return false;
  }
  std::set<std::string> tags;
  for (const auto& tag : p.tags) {
    if (!valid_token(tag, 64) || !tags.insert(tag).second) {
      diagnostic = "plugin catalog tag is invalid or duplicated";
      return false;
    }
  }
  std::set<std::string> locales;
  for (const auto& text : p.localized_text) {
    if (!valid_token(text.locale, 24) || text.name.empty() || text.name.size() > 160 ||
        text.description.empty() || text.description.size() > 2000 ||
        !locales.insert(text.locale).second) {
      diagnostic = "plugin localized metadata is invalid or duplicated";
      return false;
    }
  }
  std::tuple<int, int, int> version;
  if (!parse_version(r.minimum_engine_version, version)) {
    diagnostic = "minimum engine version must use major.minor.patch";
    return false;
  }
  diagnostic.clear();
  return true;
}

PluginCatalogCompatibility evaluate_plugin_catalog_compatibility(
    const PluginCatalogMetadata& metadata,
    const PluginHostCapabilities& host,
    std::string& diagnostic) noexcept {
  if (!validate_plugin_catalog_metadata(metadata, diagnostic))
    return PluginCatalogCompatibility::invalid_metadata;

  std::tuple<int, int, int> minimum;
  std::tuple<int, int, int> current;
  if (!parse_version(metadata.requirements.minimum_engine_version, minimum) ||
      !parse_version(host.engine_version, current)) {
    diagnostic = "host engine version is invalid";
    return PluginCatalogCompatibility::invalid_metadata;
  }
  if (current < minimum) {
    diagnostic = "plugin requires a newer DigitorEngine version";
    return PluginCatalogCompatibility::engine_too_old;
  }
  if (std::find(metadata.requirements.supported_backends.begin(),
                metadata.requirements.supported_backends.end(), host.backend) ==
      metadata.requirements.supported_backends.end()) {
    diagnostic = "selected GPU backend is not supported by this plugin";
    return PluginCatalogCompatibility::backend_unavailable;
  }
  if (metadata.requirements.requires_compute && !host.supports_compute) {
    diagnostic = "plugin requires GPU compute support";
    return PluginCatalogCompatibility::compute_required;
  }
  if (metadata.requirements.requires_fp16 && !host.supports_fp16) {
    diagnostic = "plugin requires FP16 support";
    return PluginCatalogCompatibility::fp16_required;
  }
  if (std::find(metadata.requirements.supported_pixel_formats.begin(),
                metadata.requirements.supported_pixel_formats.end(), host.pixel_format) ==
      metadata.requirements.supported_pixel_formats.end()) {
    diagnostic = "active pixel format is not supported by this plugin";
    return PluginCatalogCompatibility::unsupported_pixel_format;
  }
  diagnostic.clear();
  return PluginCatalogCompatibility::compatible;
}

const PluginLocalizedText* select_plugin_localized_text(
    const PluginCatalogMetadata& metadata,
    std::string_view preferred_locale) noexcept {
  const auto exact = std::find_if(metadata.presentation.localized_text.begin(),
                                  metadata.presentation.localized_text.end(),
                                  [preferred_locale](const auto& text) {
                                    return text.locale == preferred_locale;
                                  });
  if (exact != metadata.presentation.localized_text.end()) return &*exact;
  const auto english = std::find_if(metadata.presentation.localized_text.begin(),
                                    metadata.presentation.localized_text.end(),
                                    [](const auto& text) { return text.locale == "en"; });
  return english != metadata.presentation.localized_text.end()
      ? &*english
      : (metadata.presentation.localized_text.empty()
             ? nullptr
             : &metadata.presentation.localized_text.front());
}

}  // namespace digitor
