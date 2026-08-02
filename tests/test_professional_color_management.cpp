#include "digitor/professional_color_management.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace digitor;

namespace {
bool near(float a, float b, float tolerance = 0.03f) {
  return std::fabs(a - b) <= tolerance;
}
}

int main() {
  ColorManagementConfig config;
  config.input = ManagedColorSpace::sony_slog3_sgamut3cine;
  config.working = ManagedColorSpace::aces_cg;
  config.display = ManagedColorSpace::rec709_gamma24;
  config.output = ManagedColorSpace::rec2020_pq;
  config.display_peak_nits = 100.0;
  config.content_peak_nits = 1000.0;
  config.reference_white_nits = 100.0;
  config.tone_map = ToneMapMode::bt2390_like;
  config.gamut_map = GamutMapMode::compress;

  ProfessionalColorManagement management(config);
  HdrStaticMetadata hdr;
  hdr.mastering_present = true;
  hdr.max_luminance_nits = 1000.0;
  hdr.min_luminance_nits = 0.005;
  hdr.content_light_present = true;
  hdr.max_cll = 1000;
  hdr.max_fall = 400;
  management.set_hdr_metadata(hdr);

  const Color camera_log{0.42f, 0.38f, 0.31f, 0.75f};
  const auto working = management.to_working(camera_log);
  assert(std::isfinite(working.r));
  assert(std::isfinite(working.g));
  assert(std::isfinite(working.b));
  assert(near(working.a, camera_log.a, 0.0001f));

  const auto transformed = management.transform(camera_log);
  assert(std::isfinite(transformed.preview.r));
  assert(std::isfinite(transformed.output.r));
  assert(near(transformed.preview.a, camera_log.a, 0.0001f));
  assert(near(transformed.output.a, camera_log.a, 0.0001f));
  assert(!near(transformed.preview.r, transformed.output.r, 0.00001f));

  ColorManagementConfig roundtrip_config;
  roundtrip_config.input = ManagedColorSpace::srgb;
  roundtrip_config.working = ManagedColorSpace::linear_rec709;
  roundtrip_config.display = ManagedColorSpace::srgb;
  roundtrip_config.output = ManagedColorSpace::srgb;
  roundtrip_config.tone_map = ToneMapMode::none;
  roundtrip_config.gamut_map = GamutMapMode::none;
  ProfessionalColorManagement roundtrip(roundtrip_config);
  const Color source{0.2f, 0.5f, 0.8f, 1.0f};
  const auto restored = roundtrip.transform(source);
  assert(near(source.r, restored.output.r, 0.002f));
  assert(near(source.g, restored.output.g, 0.002f));
  assert(near(source.b, restored.output.b, 0.002f));

  std::vector<Color> image(64 * 36);
  for (std::size_t i = 0; i < image.size(); ++i) {
    const float x = static_cast<float>(i % 64) / 63.0f;
    const float y = static_cast<float>(i / 64) / 35.0f;
    image[i] = {x * 1.5f, y, (1.0f - x) * 0.8f, 1.0f};
  }
  std::vector<Color> preview(image.size()), output(image.size());
  roundtrip.transform_image(image, preview, output);
  assert(preview.size() == image.size());

  ScopeConfig scopes;
  scopes.waveform_width = 128;
  scopes.waveform_height = 64;
  scopes.vectorscope_size = 64;
  scopes.histogram_bins = 64;
  scopes.sample_step = 1;
  scopes.hdr_scale = true;
  scopes.max_nits = 1000.0;

  ScopeResult scope_result;
  std::string diagnostic;
  assert(roundtrip.generate_scopes(image, 64, 36, scopes, scope_result, &diagnostic) ==
         DIGITOR_RESULT_OK);
  assert(diagnostic.empty());
  assert(scope_result.sampled_pixels == image.size());
  assert(scope_result.waveform_luma.size() == 128u * 64u);
  assert(scope_result.waveform_rgb[0].size() == 128u * 64u);
  assert(scope_result.parade[2].size() == 128u * 64u);
  assert(scope_result.vectorscope.size() == 64u * 64u);
  assert(scope_result.cie_xy.size() == 64u * 64u);
  assert(scope_result.histogram_rgb[1].size() == 64u);
  assert(scope_result.histogram_luma.size() == 64u);
  assert(scope_result.false_color_rgba.size() == image.size() * 4u);
  assert(scope_result.peak_nits > 100.0);
  assert(scope_result.average_nits > 0.0);

  std::uint64_t histogram_total = 0;
  for (auto value : scope_result.histogram_luma) histogram_total += value;
  assert(histogram_total == image.size());

  bool gpu_dispatched = false;
  ScopeBackendCallbacks gpu_callbacks;
  gpu_callbacks.dispatch_gpu = [&gpu_dispatched](std::span<const Color>, std::uint32_t width,
                                                  std::uint32_t height, const ScopeConfig&,
                                                  ScopeResult& result, std::string& diagnostic) {
    gpu_dispatched = true;
    result.source_width = width;
    result.source_height = height;
    result.sampled_pixels = static_cast<std::uint64_t>(width) * height;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  };
  ProfessionalColorManagement gpu_scopes(roundtrip_config, gpu_callbacks);
  ScopeResult gpu_result;
  assert(gpu_scopes.generate_scopes(image, 64, 36, scopes, gpu_result, &diagnostic) ==
         DIGITOR_RESULT_OK);
  assert(gpu_dispatched);
  assert(gpu_result.sampled_pixels == image.size());

  const auto telemetry = management.telemetry();
  assert(telemetry.transformed_pixels >= 1);
  assert(telemetry.input_transform == "Sony S-Log3 S-Gamut3.Cine");
  assert(telemetry.working_space == "ACEScg");
  assert(ProfessionalColorManagement::is_hdr(ManagedColorSpace::rec2020_pq));
  assert(ProfessionalColorManagement::is_scene_linear(ManagedColorSpace::aces_cg));

  ScopeResult invalid_result;
  assert(roundtrip.generate_scopes(image, 0, 36, scopes, invalid_result, &diagnostic) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);

  std::cout << "professional color management, HDR and scopes qualification passed\n";
  return 0;
}
