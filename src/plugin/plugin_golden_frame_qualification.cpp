#include "digitor/plugin_golden_frame_qualification.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace digitor {
namespace {
bool is_hex64(const std::string& value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

bool finite_non_negative(double value) {
  return std::isfinite(value) && value >= 0.0;
}
}  // namespace

PluginGoldenFrameReport qualify_plugin_golden_frames(
    const std::vector<PluginGoldenFrameSample>& samples,
    const PluginGoldenFrameLimits& limits) {
  PluginGoldenFrameReport report;
  if (samples.empty()) {
    report.diagnostics.emplace_back("no golden-frame samples supplied");
    return report;
  }
  if (!finite_non_negative(limits.max_channel_error) ||
      !finite_non_negative(limits.mean_channel_error) ||
      !finite_non_negative(limits.max_alpha_error)) {
    report.diagnostics.emplace_back("invalid golden-frame limits");
    return report;
  }

  std::set<std::string> fixture_backend_keys;
  std::set<PluginGoldenFrameKind> covered_kinds;
  for (const auto& sample : samples) {
    const std::string key = sample.fixture_id + "\n" + sample.backend;
    if (sample.fixture_id.empty() || sample.plugin_id.empty() ||
        sample.plugin_version.empty() || sample.backend.empty() ||
        !is_hex64(sample.package_sha256) || sample.preview_digest.empty() ||
        sample.export_digest.empty()) {
      report.diagnostics.emplace_back("invalid golden-frame sample identity");
      continue;
    }
    if (!fixture_backend_keys.insert(key).second) {
      report.diagnostics.emplace_back("duplicate fixture/backend sample: " + key);
    }
    if (!finite_non_negative(sample.max_channel_error) ||
        !finite_non_negative(sample.mean_channel_error) ||
        !finite_non_negative(sample.alpha_error)) {
      report.diagnostics.emplace_back("non-finite golden-frame metric: " + sample.fixture_id);
      continue;
    }
    covered_kinds.insert(sample.kind);
    if (sample.max_channel_error > limits.max_channel_error) {
      report.diagnostics.emplace_back("maximum channel error exceeded: " + sample.fixture_id);
    }
    if (sample.mean_channel_error > limits.mean_channel_error) {
      report.diagnostics.emplace_back("mean channel error exceeded: " + sample.fixture_id);
    }
    if (sample.alpha_error > limits.max_alpha_error) {
      report.diagnostics.emplace_back("alpha error exceeded: " + sample.fixture_id);
    }
    if (limits.require_preview_export_digest_match &&
        sample.preview_digest != sample.export_digest) {
      report.diagnostics.emplace_back("preview/export digest mismatch: " + sample.fixture_id);
    }
    if (limits.require_color_metadata && !sample.color_metadata_preserved) {
      report.diagnostics.emplace_back("color metadata was not preserved: " + sample.fixture_id);
    }
    if (limits.require_deterministic_output && !sample.deterministic) {
      report.diagnostics.emplace_back("output is not deterministic: " + sample.fixture_id);
    }
  }

  for (const auto kind : {PluginGoldenFrameKind::filter,
                          PluginGoldenFrameKind::effect,
                          PluginGoldenFrameKind::transition}) {
    if (covered_kinds.count(kind) == 0) {
      report.diagnostics.emplace_back("missing filter/effect/transition golden coverage");
      break;
    }
  }

  report.qualified = report.diagnostics.empty();
  return report;
}

}  // namespace digitor
