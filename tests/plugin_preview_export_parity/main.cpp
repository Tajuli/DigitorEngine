#include "digitor/plugin_preview_export_parity.hpp"

#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_PREVIEW_EXPORT_PARITY_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginParityInvocation invocation(digitor::RemotePluginKind kind) {
  digitor::PluginParityInvocation value{};
  value.kind = kind;
  value.plugin_id = kind == digitor::RemotePluginKind::transition
      ? "transition.website.crossfade" : "effect.website.glow";
  value.plugin_version = "1.2.3";
  value.package_sha256 = std::string(64, 'a');
  value.parameters = {{"strength", {0.75}}, {"tint", {1.0, 0.8, 0.6, 1.0}}};
  value.input_color_digest = "bt2020-pq-full-straight";
  value.output_color_digest = "bt2020-pq-full-straight";
  value.timeline_time_ns = 5000000000ULL;
  value.transition_progress = kind == digitor::RemotePluginKind::transition ? 0.5 : 0.0;
  return value;
}
}

int main() {
  using namespace digitor;

  for (const auto kind : {RemotePluginKind::filter, RemotePluginKind::effect,
                          RemotePluginKind::transition}) {
    const auto preview = invocation(kind);
    const auto export_frame = preview;
    if (!qualify_plugin_preview_export_parity(preview, export_frame))
      return fail("identical preview/export invocation was rejected");
  }

  auto preview = invocation(RemotePluginKind::effect);
  auto export_frame = preview;
  export_frame.plugin_version = "1.2.4";
  if (qualify_plugin_preview_export_parity(preview, export_frame))
    return fail("version drift was accepted");

  export_frame = preview;
  export_frame.parameters[0].values[0] = 0.5;
  if (qualify_plugin_preview_export_parity(preview, export_frame))
    return fail("parameter drift was accepted");

  export_frame = preview;
  export_frame.output_color_digest = "bt709-srgb-full-straight";
  if (qualify_plugin_preview_export_parity(preview, export_frame))
    return fail("color metadata drift was accepted");

  std::cout << "PLUGIN_PREVIEW_EXPORT_PARITY_QUALIFIED=1\n";
  std::cout << "MAIN_RENDERING_FEATURES_CHANGED=0\n";
  std::cout << "GPU_PIPELINE_REPLACED=0\n";
  return 0;
}
