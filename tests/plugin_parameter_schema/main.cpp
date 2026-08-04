#include "digitor/remote_plugin_marketplace.hpp"

#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_PARAMETER_SCHEMA_FAILED=" << message << '\n';
  return 1;
}
}

int main() {
  using namespace digitor;
  std::string diagnostic;

  RemotePluginParameter color{};
  color.id = "tint";
  color.label = "Tint";
  color.minimum = 0.0;
  color.maximum = 1.0;
  color.default_value = 1.0;
  color.ui.type = PluginParameterType::color_rgba;
  color.ui.control = PluginParameterControl::color_picker;
  color.ui.group = "Color";
  color.ui.component_count = 4;
  color.ui.default_components = {1.0, 1.0, 1.0, 1.0};

  RemotePluginParameter blend{};
  blend.id = "blend_mode";
  blend.label = "Blend Mode";
  blend.minimum = 0.0;
  blend.maximum = 2.0;
  blend.default_value = 0.0;
  blend.ui.type = PluginParameterType::enumeration;
  blend.ui.control = PluginParameterControl::dropdown;
  blend.ui.group = "Composite";
  blend.ui.options = {{"normal", "Normal", 0.0},
                      {"screen", "Screen", 1.0},
                      {"overlay", "Overlay", 2.0}};

  RemotePluginParameter center{};
  center.id = "center";
  center.label = "Center";
  center.ui.type = PluginParameterType::point2d;
  center.ui.control = PluginParameterControl::point_editor;
  center.ui.component_count = 2;
  center.ui.default_components = {0.5, 0.5};

  RemotePluginCatalogEntry entry{};
  entry.id = "effect.typed.parameters";
  entry.display_name = "Typed Parameters";
  entry.version = "1.0.0";
  entry.minimum_engine_version = "4.9.0";
  entry.kind = RemotePluginKind::effect;
  entry.publisher_key_id = "digitor.official";
  entry.signature = "qualified";
  entry.parameters = {color, blend, center};
  entry.artifacts.push_back({RemotePluginBackend::windows_d3d12,
                             "https://plugins.example/effect.digitorfx",
                             std::string(64, 'a'), "effect.digitorfx"});

  if (!validate_remote_plugin_catalog_entry(entry, diagnostic))
    return fail(diagnostic.c_str());

  const auto payload = canonical_remote_plugin_payload(entry);
  if (payload.find("Color") == std::string::npos ||
      payload.find("overlay") == std::string::npos)
    return fail("typed UI metadata missing from signed canonical payload");

  auto invalid = color;
  invalid.ui.default_components = {1.0, 1.0, 1.0};
  if (validate_plugin_parameter_ui_schema(invalid.ui, diagnostic))
    return fail("invalid RGBA component schema accepted");

  std::cout << "PLUGIN_PARAMETER_SCHEMA_QUALIFIED=1\n";
  std::cout << "CODE_FREE_UI_CONTROLS=1\n";
  std::cout << "SIGNED_TYPED_METADATA=1\n";
  return 0;
}
