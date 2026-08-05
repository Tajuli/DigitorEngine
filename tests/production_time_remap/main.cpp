#include "digitor/production_time_remap.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

int main() {
  using namespace digitor;
  const std::vector<TimeRemapKeyframe> keys{{0.0, 0.0, 1.0, 1.0}, {1.0, 1.0, 1.0, 2.0}, {2.0, 3.0, 2.0, 2.0}};
  TimeRemapSettings settings;
  settings.interpolation = TimeRemapInterpolation::blend;
  settings.source_duration_seconds = 4.0;
  const auto preview = build_time_remap_plan(keys, 2.0, 30.0, 30.0, settings);
  const auto export_plan = build_time_remap_plan(keys, 2.0, 30.0, 30.0, settings);
  if (preview.samples.size() != 60u || preview.digest == 0u) return 1;
  if (preview.digest != export_plan.digest) return 2;
  if (preview.samples.front().source_frame_a != 0u) return 3;
  if (preview.samples.back().source_seconds < 2.8 || preview.samples.back().source_seconds > 3.1) return 4;
  if (preview.samples[20].speed <= 0.0) return 5;

  for (const auto backend : {TimeRemapBackend::vulkan, TimeRemapBackend::d3d12,
                             TimeRemapBackend::metal, TimeRemapBackend::gles}) {
    TimeRemapDispatchPacket packet;
    packet.backend = backend;
    packet.command_handle = 1u;
    packet.source_a_handle = 2u;
    packet.source_b_handle = 3u;
    packet.output_handle = 4u;
    packet.width = 1920u;
    packet.height = 1080u;
    packet.blend = 0.5f;
    if (dispatch_time_remap_gpu(packet, [](const auto&) { return true; }).status != TimeRemapStatus::ready) return 6;
  }

  TimeRemapDispatchPacket invalid;
  invalid.backend = TimeRemapBackend::vulkan;
  if (dispatch_time_remap_gpu(invalid, [](const auto&) { return true; }).status != TimeRemapStatus::invalid) return 7;

  const DigitorTimeRemapKeyframe ckeys[]{{0.0, 0.0, 1.0, 1.0}, {1.0, 1.0, 1.0, 1.0}};
  DigitorTimeRemapSettings csettings{1u, 1u, 1u, 2.0};
  std::vector<double> source(30u);
  std::vector<float> blend(30u);
  std::size_t count{};
  std::uint64_t digest{};
  if (digitor_time_remap_build(ckeys, 2u, 1.0, 30.0, 30.0, &csettings,
                               source.data(), blend.data(), source.size(), &count, &digest) != 0u) return 8;
  if (count != 30u || digest == 0u) return 9;
  return 0;
}
