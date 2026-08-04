#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace digitor {

enum class PluginParameterType : std::uint32_t {
  floating,
  integer,
  boolean,
  enumeration,
  color_rgba,
  point2d,
  vector3,
  angle,
  curve,
  gradient,
  asset,
  texture,
  text
};

enum class PluginParameterControl : std::uint32_t {
  automatic,
  slider,
  number,
  toggle,
  dropdown,
  color_picker,
  point_editor,
  vector_editor,
  curve_editor,
  gradient_editor,
  asset_picker,
  text_field
};

struct PluginParameterOption final {
  std::string id;
  std::string label;
  double numeric_value{};
};

struct PluginParameterUiSchema final {
  PluginParameterType type{PluginParameterType::floating};
  PluginParameterControl control{PluginParameterControl::automatic};
  std::string group;
  std::string unit;
  std::uint32_t precision{3};
  std::uint32_t component_count{1};
  std::vector<double> default_components;
  std::string default_text;
  std::vector<PluginParameterOption> options;
  std::string visible_when_parameter;
  std::string visible_when_option;
};

[[nodiscard]] bool validate_plugin_parameter_ui_schema(
    const PluginParameterUiSchema& schema,
    std::string& diagnostic) noexcept;

[[nodiscard]] std::string canonical_plugin_parameter_ui_schema(
    const PluginParameterUiSchema& schema);

}  // namespace digitor
