#include "digitor/plugin_parameter_schema.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace digitor {
namespace {

bool valid_token(std::string_view value) noexcept {
  if (value.empty() || value.size() > 160) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '.' || c == '_' || c == '-';
  });
}

bool valid_optional_token(std::string_view value) noexcept {
  return value.empty() || valid_token(value);
}

bool requires_components(PluginParameterType type) noexcept {
  return type == PluginParameterType::color_rgba ||
         type == PluginParameterType::point2d ||
         type == PluginParameterType::vector3;
}

std::uint32_t expected_components(PluginParameterType type) noexcept {
  switch (type) {
    case PluginParameterType::color_rgba: return 4;
    case PluginParameterType::point2d: return 2;
    case PluginParameterType::vector3: return 3;
    default: return 1;
  }
}

}  // namespace

bool validate_plugin_parameter_ui_schema(
    const PluginParameterUiSchema& schema,
    std::string& diagnostic) noexcept {
  if (schema.group.size() > 160 || schema.unit.size() > 32 ||
      schema.default_text.size() > 4096 || schema.precision > 9 ||
      schema.options.size() > 128 || schema.default_components.size() > 16 ||
      !valid_optional_token(schema.visible_when_parameter) ||
      !valid_optional_token(schema.visible_when_option)) {
    diagnostic = "plugin parameter UI metadata exceeds limits";
    return false;
  }

  if (requires_components(schema.type)) {
    const auto expected = expected_components(schema.type);
    if (schema.component_count != expected ||
        schema.default_components.size() != expected) {
      diagnostic = "plugin parameter component schema is invalid";
      return false;
    }
  } else if (schema.component_count == 0 || schema.component_count > 16) {
    diagnostic = "plugin parameter component count is invalid";
    return false;
  }

  if (schema.type == PluginParameterType::enumeration) {
    if (schema.options.empty()) {
      diagnostic = "enumeration parameter requires options";
      return false;
    }
    std::vector<std::string> ids;
    for (const auto& option : schema.options) {
      if (!valid_token(option.id) || option.label.empty() ||
          std::find(ids.begin(), ids.end(), option.id) != ids.end()) {
        diagnostic = "plugin parameter option schema is invalid";
        return false;
      }
      ids.push_back(option.id);
    }
  } else if (!schema.options.empty()) {
    diagnostic = "only enumeration parameters may declare options";
    return false;
  }

  if ((schema.type == PluginParameterType::text ||
       schema.type == PluginParameterType::asset ||
       schema.type == PluginParameterType::texture) &&
      schema.default_text.size() > 4096) {
    diagnostic = "plugin parameter text default is oversized";
    return false;
  }

  if (schema.visible_when_parameter.empty() !=
      schema.visible_when_option.empty()) {
    diagnostic = "plugin parameter visibility condition is incomplete";
    return false;
  }

  diagnostic.clear();
  return true;
}

std::string canonical_plugin_parameter_ui_schema(
    const PluginParameterUiSchema& schema) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << static_cast<std::uint32_t>(schema.type) << '|'
      << static_cast<std::uint32_t>(schema.control) << '|'
      << schema.group << '|' << schema.unit << '|'
      << schema.precision << '|' << schema.component_count << '|'
      << schema.default_text << '|'
      << schema.visible_when_parameter << '|'
      << schema.visible_when_option;
  for (double value : schema.default_components) out << "|c:" << value;
  for (const auto& option : schema.options)
    out << "|o:" << option.id << ':' << option.label << ':' << option.numeric_value;
  return out.str();
}

}  // namespace digitor
