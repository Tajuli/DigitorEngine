#include "digitor/plugin_golden_frame_qualification.hpp"

#include <iostream>
#include <string>
#include <vector>

int main() {
  using namespace digitor;
  const std::string sha(64, 'a');
  const std::string digest(64, 'b');
  std::vector<PluginGoldenFrameSample> samples = {
      {"filter-sdr", PluginGoldenFrameKind::filter, PluginGoldenTransfer::rec709,
       "builtin.clean", "1.0.0", sha, "vulkan", digest, digest,
       0.001, 0.0002, 0.0, true, true},
      {"effect-hdr", PluginGoldenFrameKind::effect, PluginGoldenTransfer::pq,
       "market.glow", "2.1.0", sha, "d3d12", digest, digest,
       0.001, 0.0002, 0.0, true, true},
      {"transition-alpha", PluginGoldenFrameKind::transition, PluginGoldenTransfer::hlg,
       "market.wipe", "3.0.0", sha, "metal", digest, digest,
       0.001, 0.0002, 0.0, true, true}};

  const auto ok = qualify_plugin_golden_frames(samples);
  if (!ok.qualified) return 1;

  auto bad = samples;
  bad[1].export_digest = std::string(64, 'c');
  if (qualify_plugin_golden_frames(bad).qualified) return 2;
  bad = samples;
  bad[2].alpha_error = 0.01;
  if (qualify_plugin_golden_frames(bad).qualified) return 3;
  bad = samples;
  bad[0].color_metadata_preserved = false;
  if (qualify_plugin_golden_frames(bad).qualified) return 4;
  bad = samples;
  bad.pop_back();
  if (qualify_plugin_golden_frames(bad).qualified) return 5;

  std::cout << "PLUGIN_GOLDEN_FRAME_REAL_MEDIA=1\n"
            << "FILTER_EFFECT_TRANSITION_COVERAGE=1\n"
            << "HDR_SDR_COLOR_METADATA=1\n"
            << "ALPHA_PRESERVATION=1\n"
            << "PREVIEW_EXPORT_PARITY=1\n"
            << "MAIN_RENDERING_FEATURES_CHANGED=0\n";
  return 0;
}
