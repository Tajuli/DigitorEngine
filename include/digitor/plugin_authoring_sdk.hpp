#pragma once

#include "digitor/remote_plugin_marketplace.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

enum class PluginAuthoringKind : std::uint32_t {
  filter,
  effect,
  transition
};

enum class PluginAuthoringParameterType : std::uint32_t {
  number,
  boolean,
  enumeration,
  color_rgba,
  point2d,
  vector3,
  curve,
  gradient,
  asset,
  text
};

struct PluginAuthoringLocalizedText final {
  std::string locale;
  std::string name;
  std::string description;
};

struct PluginAuthoringParameter final {
  std::string id;
  PluginAuthoringParameterType type{PluginAuthoringParameterType::number};
  std::string label;
  std::string group;
  double minimum{};
  double maximum{1.0};
  double default_value{};
  bool keyframeable{true};
  std::vector<std::string> enum_values;
};

struct PluginAuthoringArtifact final {
  RemotePluginBackend backend{RemotePluginBackend::windows_d3d12};
  std::string relative_path;
  std::string sha256;
};

struct PluginAuthoringManifest final {
  std::string plugin_id;
  std::string version;
  PluginAuthoringKind kind{PluginAuthoringKind::filter};
  std::string publisher_id;
  std::string minimum_engine_version;
  std::string category;
  std::vector<std::string> tags;
  std::vector<PluginAuthoringLocalizedText> localized_text;
  std::vector<PluginAuthoringParameter> parameters;
  std::vector<PluginAuthoringArtifact> artifacts;
  bool preserves_alpha{true};
  bool deterministic{true};
};

struct PluginAuthoringFile final {
  std::string relative_path;
  std::string sha256;
  std::uint64_t size_bytes{};
};

struct PluginAuthoringPackagePlan final {
  std::string package_file_name;
  std::string canonical_manifest;
  std::string signing_payload;
  std::vector<PluginAuthoringFile> files;
};

[[nodiscard]] bool validate_plugin_authoring_manifest(
    const PluginAuthoringManifest& manifest,
    std::string& diagnostic) noexcept;

[[nodiscard]] bool build_plugin_authoring_package_plan(
    const PluginAuthoringManifest& manifest,
    const std::vector<PluginAuthoringFile>& files,
    PluginAuthoringPackagePlan& out_plan,
    std::string& diagnostic) noexcept;

[[nodiscard]] std::string plugin_authoring_kind_name(
    PluginAuthoringKind kind);

[[nodiscard]] std::string plugin_authoring_backend_name(
    RemotePluginBackend backend);

}  // namespace digitor
