#include "digitor/transition_subsystem.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

digitor::PluginTransitionRequest gpu_request(std::string transition_id) {
  using namespace digitor;
  PluginTransitionRequest request;
  request.instance.instance_id = "transition-1";
  request.instance.plugin_id = std::move(transition_id);
  request.instance.plugin_version = "1";
  request.instance.progress = 0.5;
  request.project_or_clip_id = "clip-1";
  request.visual_stack_digest = "stack-1";
  request.outgoing.native_texture_handle = 1u;
  request.incoming.native_texture_handle = 2u;
  request.output.native_texture_handle = 3u;
  request.outgoing.width = request.incoming.width = request.output.width = 2u;
  request.outgoing.height = request.incoming.height = request.output.height = 1u;
  request.outgoing.format = request.incoming.format = request.output.format =
      PluginPixelFormat::rgba32_float;
  return request;
}

}  // namespace

int main() {
  using namespace digitor;

  bool built_in_recorded = false;
  bool plugin_recorded = false;
  TransitionSubsystem subsystem({
      RemotePluginBackend::windows_d3d12,
      [&built_in_recorded](const PluginTransitionRequest& request,
                           const TransitionSettings& settings,
                           std::string&) {
        built_in_recorded = request.instance.plugin_id == "builtin.cross-dissolve" &&
                            settings.type == TransitionType::cross_dissolve;
        return built_in_recorded ? DIGITOR_RESULT_OK : DIGITOR_RESULT_INVALID_ARGUMENT;
      },
      [&plugin_recorded](const PluginTransitionDispatch& dispatch, std::string&) {
        plugin_recorded = dispatch.request.instance.plugin_id == "plugin.vendor.zoom";
        return plugin_recorded ? DIGITOR_RESULT_OK : DIGITOR_RESULT_INVALID_ARGUMENT;
      },
  });

  if (subsystem.descriptors().size() != 4u ||
      !subsystem.find("builtin.cross-dissolve")) {
    return 1;
  }
  std::string diagnostic;
  if (!subsystem.register_plugin(
          {"plugin.vendor.zoom", "Vendor Zoom", "2.0", TransitionProviderKind::plugin},
          &diagnostic)) {
    return 2;
  }
  if (subsystem.register_plugin(
          {"builtin.wipe", "Conflict", "1", TransitionProviderKind::plugin},
          &diagnostic)) {
    return 3;
  }

  auto built_in = gpu_request("builtin.cross-dissolve");
  if (subsystem.dispatch(built_in, &diagnostic) != DIGITOR_RESULT_OK ||
      !built_in_recorded) {
    return 4;
  }
  auto plugin = gpu_request("plugin.vendor.zoom");
  plugin.instance.plugin_version = "2.0";
  if (subsystem.dispatch(plugin, &diagnostic) != DIGITOR_RESULT_OK ||
      !plugin_recorded) {
    return 5;
  }
  auto unknown = gpu_request("plugin.unknown");
  if (subsystem.dispatch(unknown, &diagnostic) != DIGITOR_RESULT_INVALID_ARGUMENT) {
    return 6;
  }

  TransitionFrame a{2u, 1u, {{1.0f, 0.0f, 0.0f, 1.0f},
                              {1.0f, 0.0f, 0.0f, 1.0f}}};
  TransitionFrame b{2u, 1u, {{0.0f, 0.0f, 1.0f, 1.0f},
                              {0.0f, 0.0f, 1.0f, 1.0f}}};
  TransitionSettings settings;
  settings.progress = 0.5f;
  TransitionFrame preview;
  TransitionFrame export_frame;
  const auto preview_result = subsystem.render_reference(
      "builtin.cross-dissolve", a, b, preview, settings);
  const auto export_result = subsystem.render_reference(
      "builtin.cross-dissolve", a, b, export_frame, settings);
  if (preview_result.status != TransitionStatus::ready ||
      preview_result.digest != export_result.digest) {
    return 7;
  }
  if (subsystem.render_reference("plugin.vendor.zoom", a, b, preview, settings).status !=
      TransitionStatus::invalid) {
    return 8;
  }

  std::vector<float> packed_a(8u);
  std::vector<float> packed_b(8u);
  std::vector<float> packed_output(8u);
  for (std::size_t index = 0; index < 2u; ++index) {
    packed_a[index * 4u] = 1.0f;
    packed_a[index * 4u + 3u] = 1.0f;
    packed_b[index * 4u + 2u] = 1.0f;
    packed_b[index * 4u + 3u] = 1.0f;
  }
  DigitorTransitionSettings c_settings{};
  c_settings.progress = 0.5f;
  c_settings.softness = 0.02f;
  c_settings.dip_a = 1.0f;
  c_settings.ease_in_out = 1u;
  std::uint64_t digest{};
  if (digitor_transition_rgba32f(packed_a.data(), packed_b.data(),
                                  packed_output.data(), 2u, 1u,
                                  &c_settings, &digest) != 0u ||
      digest != preview_result.digest) {
    return 9;
  }
  if (digitor_builtin_transition_count() != 4u) return 10;
  char id[64]{};
  if (digitor_builtin_transition_id(0u, id, sizeof(id)) != 0u ||
      std::string(id) != "builtin.cross-dissolve") {
    return 11;
  }
  return 0;
}
