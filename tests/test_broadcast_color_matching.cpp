#include "digitor/broadcast_color_matching.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace digitor;

int main() {
  BroadcastColorSuite suite;
  std::string diagnostic;

  std::vector<Color> unsafe{{-0.1f, 0.2f, 0.2f, 1.0f},
                            {1.2f, 1.1f, 1.0f, 1.0f},
                            {0.5f, 0.5f, 0.5f, 1.0f},
                            {0.3f, 0.4f, 0.5f, 1.0f}};
  BroadcastMonitorConfig monitor;
  BroadcastMonitorResult result;
  assert(suite.monitor(unsafe, 2, 2, monitor, result, &diagnostic) ==
         DIGITOR_RESULT_OK);
  assert(!result.broadcast_safe);
  assert(result.violations.gamut_illegal >= 2u);
  assert(result.warning_mask.size() == unsafe.size());

  std::vector<Color> source{{0.2f, 0.3f, 0.4f, 0.5f},
                            {0.4f, 0.5f, 0.6f, 1.0f}};
  std::vector<Color> reference{{0.4f, 0.45f, 0.5f, 0.5f},
                               {0.8f, 0.75f, 0.7f, 1.0f}};
  ColorMatchRequest request;
  request.method = MatchMethod::reference_frame;
  request.source = source;
  request.reference = reference;
  request.strength = 1.0;
  ColorMatchTransform transform;
  assert(suite.build_match(request, transform, &diagnostic) == DIGITOR_RESULT_OK);
  assert(transform.mean_error_after <= transform.mean_error_before);

  std::vector<Color> matched(source.size());
  assert(suite.apply_match(source, matched, transform, 1.0, &diagnostic) ==
         DIGITOR_RESULT_OK);
  assert(matched[0].a == source[0].a);

  std::vector<ColorPatch> patches(6);
  for (std::size_t index = 0; index < patches.size(); ++index) {
    const float value = static_cast<float>(index + 1u) / 10.0f;
    patches[index].measured = {value, value * 0.9f, value * 0.8f, 1.0f};
    patches[index].reference = {value * 1.1f, value, value * 0.9f, 1.0f};
  }
  ColorMatchRequest chart;
  chart.method = MatchMethod::chart_24_patch;
  chart.chart_patches = patches;
  assert(suite.build_match(chart, transform, &diagnostic) == DIGITOR_RESULT_OK);

  BroadcastMonitorConfig invalid = monitor;
  invalid.safe_area.left = 0.8;
  invalid.safe_area.right = 0.3;
  assert(suite.monitor(unsafe, 2, 2, invalid, result, &diagnostic) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);

  const auto telemetry = suite.telemetry();
  assert(telemetry.monitored_frames == 1u);
  assert(telemetry.unsafe_frames == 1u);
  assert(telemetry.match_requests == 2u);
  assert(telemetry.matched_pixels == source.size());
  return 0;
}
