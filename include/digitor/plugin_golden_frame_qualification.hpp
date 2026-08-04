#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class PluginGoldenFrameKind : std::uint32_t {
  filter = 0,
  effect = 1,
  transition = 2,
};

enum class PluginGoldenTransfer : std::uint32_t {
  srgb = 0,
  rec709 = 1,
  pq = 2,
  hlg = 3,
};

struct PluginGoldenFrameSample {
  std::string fixture_id;
  PluginGoldenFrameKind kind = PluginGoldenFrameKind::filter;
  PluginGoldenTransfer transfer = PluginGoldenTransfer::rec709;
  std::string plugin_id;
  std::string plugin_version;
  std::string package_sha256;
  std::string backend;
  std::string preview_digest;
  std::string export_digest;
  double max_channel_error = 0.0;
  double mean_channel_error = 0.0;
  double alpha_error = 0.0;
  bool color_metadata_preserved = false;
  bool deterministic = false;
};

struct PluginGoldenFrameLimits {
  double max_channel_error = 1.0 / 255.0;
  double mean_channel_error = 0.25 / 255.0;
  double max_alpha_error = 0.0;
  bool require_preview_export_digest_match = true;
  bool require_color_metadata = true;
  bool require_deterministic_output = true;
};

struct PluginGoldenFrameReport {
  bool qualified = false;
  std::vector<std::string> diagnostics;
};

PluginGoldenFrameReport qualify_plugin_golden_frames(
    const std::vector<PluginGoldenFrameSample>& samples,
    const PluginGoldenFrameLimits& limits = {});

}  // namespace digitor
