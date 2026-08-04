#include "digitor/plugin_authoring_sdk.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace digitor {
namespace {

bool valid_token(std::string_view value, std::size_t max_size) noexcept {
  if (value.empty() || value.size() > max_size) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '_' || c == '-';
  });
}

bool valid_sha256(std::string_view value) noexcept {
  return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isdigit(c) || (c >= 'a' && c <= 'f');
  });
}

bool safe_relative_path(std::string_view value) noexcept {
  if (value.empty() || value.size() > 512 || value.front() == '/' || value.front() == '\\')
    return false;
  if (value.find("..") != std::string_view::npos || value.find(':') != std::string_view::npos)
    return false;
  return std::none_of(value.begin(), value.end(), [](unsigned char c) {
    return c == '\0' || c < 0x20;
  });
}

std::string escape(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if (c == '\\' || c == '"') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

std::string parameter_type_name(PluginAuthoringParameterType type) {
  switch (type) {
    case PluginAuthoringParameterType::number: return "number";
    case PluginAuthoringParameterType::boolean: return "boolean";
    case PluginAuthoringParameterType::enumeration: return "enum";
    case PluginAuthoringParameterType::color_rgba: return "color_rgba";
    case PluginAuthoringParameterType::point2d: return "point2d";
    case PluginAuthoringParameterType::vector3: return "vector3";
    case PluginAuthoringParameterType::curve: return "curve";
    case PluginAuthoringParameterType::gradient: return "gradient";
    case PluginAuthoringParameterType::asset: return "asset";
    case PluginAuthoringParameterType::text: return "text";
  }
  return "unknown";
}

}  // namespace

std::string plugin_authoring_kind_name(PluginAuthoringKind kind) {
  switch (kind) {
    case PluginAuthoringKind::filter: return "filter";
    case PluginAuthoringKind::effect: return "effect";
    case PluginAuthoringKind::transition: return "transition";
  }
  return "unknown";
}

std::string plugin_authoring_backend_name(RemotePluginBackend backend) {
  switch (backend) {
    case RemotePluginBackend::windows_d3d12: return "windows-d3d12";
    case RemotePluginBackend::windows_vulkan: return "windows-vulkan";
    case RemotePluginBackend::android_vulkan: return "android-vulkan";
    case RemotePluginBackend::android_gles: return "android-gles";
    case RemotePluginBackend::apple_metal: return "apple-metal";
  }
  return "unknown";
}

bool validate_plugin_authoring_manifest(const PluginAuthoringManifest& manifest,
                                        std::string& diagnostic) noexcept {
  try {
    if (!valid_token(manifest.plugin_id, 160) || !valid_token(manifest.version, 48) ||
        !valid_token(manifest.publisher_id, 120) ||
        !valid_token(manifest.minimum_engine_version, 48) ||
        !valid_token(manifest.category, 80)) {
      diagnostic = "plugin identity or compatibility metadata is invalid";
      return false;
    }
    if (manifest.localized_text.empty() || manifest.localized_text.size() > 32 ||
        manifest.parameters.size() > 256 || manifest.artifacts.empty() ||
        manifest.artifacts.size() > 16 || manifest.tags.size() > 32) {
      diagnostic = "plugin manifest collection limit is invalid";
      return false;
    }
    if (!manifest.preserves_alpha || !manifest.deterministic) {
      diagnostic = "plugin must preserve alpha and declare deterministic processing";
      return false;
    }
    std::set<std::string> locales;
    for (const auto& text : manifest.localized_text) {
      if (!valid_token(text.locale, 24) || text.name.empty() || text.name.size() > 160 ||
          text.description.empty() || text.description.size() > 2000 ||
          !locales.insert(text.locale).second) {
        diagnostic = "localized plugin metadata is invalid or duplicated";
        return false;
      }
    }
    std::set<std::string> parameter_ids;
    for (const auto& parameter : manifest.parameters) {
      if (!valid_token(parameter.id, 96) || parameter.label.empty() ||
          !std::isfinite(parameter.minimum) || !std::isfinite(parameter.maximum) ||
          !std::isfinite(parameter.default_value) || parameter.minimum > parameter.maximum ||
          parameter.default_value < parameter.minimum || parameter.default_value > parameter.maximum ||
          !parameter_ids.insert(parameter.id).second) {
        diagnostic = "plugin parameter schema is invalid or duplicated";
        return false;
      }
      if (parameter.type == PluginAuthoringParameterType::enumeration &&
          parameter.enum_values.empty()) {
        diagnostic = "enumeration parameter has no values";
        return false;
      }
    }
    std::set<RemotePluginBackend> backends;
    std::set<std::string> artifact_paths;
    for (const auto& artifact : manifest.artifacts) {
      if (!safe_relative_path(artifact.relative_path) || !valid_sha256(artifact.sha256) ||
          !backends.insert(artifact.backend).second ||
          !artifact_paths.insert(artifact.relative_path).second) {
        diagnostic = "backend artifact is invalid or duplicated";
        return false;
      }
    }
    diagnostic.clear();
    return true;
  } catch (...) {
    diagnostic = "plugin authoring validation failed";
    return false;
  }
}

bool build_plugin_authoring_package_plan(const PluginAuthoringManifest& manifest,
                                         const std::vector<PluginAuthoringFile>& files,
                                         PluginAuthoringPackagePlan& out_plan,
                                         std::string& diagnostic) noexcept {
  out_plan = {};
  if (!validate_plugin_authoring_manifest(manifest, diagnostic)) return false;
  try {
    if (files.empty() || files.size() > 1024) {
      diagnostic = "plugin package file list is empty or exceeds limits";
      return false;
    }
    std::vector<PluginAuthoringFile> sorted_files = files;
    std::sort(sorted_files.begin(), sorted_files.end(), [](const auto& a, const auto& b) {
      return a.relative_path < b.relative_path;
    });
    std::set<std::string> paths;
    std::uint64_t total_size = 0;
    for (const auto& file : sorted_files) {
      if (!safe_relative_path(file.relative_path) || !valid_sha256(file.sha256) ||
          file.size_bytes == 0 || !paths.insert(file.relative_path).second) {
        diagnostic = "plugin package file entry is invalid or duplicated";
        return false;
      }
      total_size += file.size_bytes;
      if (total_size > 512ull * 1024ull * 1024ull) {
        diagnostic = "plugin package exceeds expanded-size limit";
        return false;
      }
    }

    std::ostringstream canonical;
    canonical << std::setprecision(17);
    canonical << "{\"plugin_id\":\"" << escape(manifest.plugin_id)
              << "\",\"version\":\"" << escape(manifest.version)
              << "\",\"kind\":\"" << plugin_authoring_kind_name(manifest.kind)
              << "\",\"publisher_id\":\"" << escape(manifest.publisher_id)
              << "\",\"minimum_engine_version\":\"" << escape(manifest.minimum_engine_version)
              << "\",\"category\":\"" << escape(manifest.category) << "\",\"parameters\":[";
    for (std::size_t i = 0; i < manifest.parameters.size(); ++i) {
      const auto& p = manifest.parameters[i];
      if (i) canonical << ',';
      canonical << "{\"id\":\"" << escape(p.id) << "\",\"type\":\""
                << parameter_type_name(p.type) << "\",\"min\":" << p.minimum
                << ",\"max\":" << p.maximum << ",\"default\":" << p.default_value
                << ",\"keyframeable\":" << (p.keyframeable ? "true" : "false") << '}';
    }
    canonical << "],\"artifacts\":[";
    auto artifacts = manifest.artifacts;
    std::sort(artifacts.begin(), artifacts.end(), [](const auto& a, const auto& b) {
      return plugin_authoring_backend_name(a.backend) < plugin_authoring_backend_name(b.backend);
    });
    for (std::size_t i = 0; i < artifacts.size(); ++i) {
      if (i) canonical << ',';
      canonical << "{\"backend\":\"" << plugin_authoring_backend_name(artifacts[i].backend)
                << "\",\"path\":\"" << escape(artifacts[i].relative_path)
                << "\",\"sha256\":\"" << artifacts[i].sha256 << "\"}";
    }
    canonical << "]}";

    std::ostringstream signing;
    signing << canonical.str() << '\n';
    for (const auto& file : sorted_files)
      signing << file.relative_path << ':' << file.sha256 << ':' << file.size_bytes << '\n';

    out_plan.package_file_name = manifest.plugin_id + "-" + manifest.version + ".digitorfx";
    out_plan.canonical_manifest = canonical.str();
    out_plan.signing_payload = signing.str();
    out_plan.files = std::move(sorted_files);
    diagnostic.clear();
    return true;
  } catch (...) {
    out_plan = {};
    diagnostic = "plugin package planning failed";
    return false;
  }
}

}  // namespace digitor
