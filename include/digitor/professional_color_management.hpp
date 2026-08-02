#pragma once

#include "digitor/color.hpp"
#include "digitor/color_science.hpp"
#include "digitor/digitor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace digitor {

enum class ManagedColorSpace {
  unknown,
  rec709_gamma24,
  srgb,
  display_p3,
  rec2020_gamma24,
  rec2020_pq,
  rec2020_hlg,
  linear_rec709,
  linear_rec2020,
  aces_cg,
  aces_cct,
  sony_slog3_sgamut3cine,
  canon_log2_cinema_gamut,
  panasonic_vlog_vgamut,
  arri_logc3_wide_gamut,
  blackmagic_film_gen5
};

enum class ColorPipelineMode { scene_referred, display_referred };
enum class GamutMapMode { none, clip, compress };
enum class ToneMapMode { none, reinhard, hable, bt2390_like };
enum class ScopeKind {
  waveform_luma,
  waveform_rgb,
  rgb_parade,
  vectorscope,
  histogram_rgb,
  histogram_luma,
  cie_xy,
  false_color
};

struct HdrStaticMetadata {
  bool mastering_present{};
  std::array<double, 8> display_primaries_and_white_xy{};
  double min_luminance_nits{};
  double max_luminance_nits{};
  bool content_light_present{};
  std::uint16_t max_cll{};
  std::uint16_t max_fall{};
};

struct ColorManagementConfig {
  ManagedColorSpace input{ManagedColorSpace::rec709_gamma24};
  ManagedColorSpace working{ManagedColorSpace::aces_cct};
  ManagedColorSpace display{ManagedColorSpace::rec709_gamma24};
  ManagedColorSpace output{ManagedColorSpace::rec709_gamma24};
  ColorPipelineMode mode{ColorPipelineMode::scene_referred};
  GamutMapMode gamut_map{GamutMapMode::compress};
  ToneMapMode tone_map{ToneMapMode::bt2390_like};
  double reference_white_nits{100.0};
  double display_peak_nits{100.0};
  double content_peak_nits{1000.0};
  bool preserve_negative_values{true};
  bool legal_range_output{};
  bool enable_display_transform{true};
  bool enable_output_transform{true};
};

struct ColorPipelineTelemetry {
  std::uint64_t transformed_pixels{};
  std::uint64_t non_finite_pixels{};
  std::uint64_t gamut_compressions{};
  std::uint64_t tone_mapped_pixels{};
  std::uint64_t clipped_pixels{};
  std::uint64_t scope_frames{};
  std::string input_transform;
  std::string working_space;
  std::string display_transform;
  std::string output_transform;
};

struct ScopeConfig {
  std::vector<ScopeKind> enabled{
      ScopeKind::waveform_luma, ScopeKind::rgb_parade,
      ScopeKind::vectorscope, ScopeKind::histogram_rgb};
  std::uint32_t waveform_width{512};
  std::uint32_t waveform_height{256};
  std::uint32_t vectorscope_size{256};
  std::uint32_t histogram_bins{256};
  std::uint32_t sample_step{1};
  double max_nits{1000.0};
  bool skin_tone_indicator{true};
  bool hdr_scale{};
};

struct ScopeResult {
  std::uint32_t source_width{};
  std::uint32_t source_height{};
  std::uint64_t sampled_pixels{};
  std::vector<std::uint32_t> waveform_luma;
  std::array<std::vector<std::uint32_t>, 3> waveform_rgb;
  std::array<std::vector<std::uint32_t>, 3> parade;
  std::vector<std::uint32_t> vectorscope;
  std::array<std::vector<std::uint32_t>, 3> histogram_rgb;
  std::vector<std::uint32_t> histogram_luma;
  std::vector<std::uint32_t> cie_xy;
  std::vector<std::uint8_t> false_color_rgba;
  double peak_nits{};
  double average_nits{};
  double skin_tone_angle_degrees{123.0};
};

struct ScopeBackendCallbacks {
  std::function<DigitorResult(std::span<const Color>, std::uint32_t, std::uint32_t,
                              const ScopeConfig&, ScopeResult&, std::string&)> dispatch_gpu;
};

struct ColorTransformResult {
  Color preview{};
  Color output{};
};

class ProfessionalColorManagement {
 public:
  explicit ProfessionalColorManagement(ColorManagementConfig config,
                                       ScopeBackendCallbacks scopes = {});

  const ColorManagementConfig& config() const noexcept { return config_; }
  void set_config(ColorManagementConfig config);
  void set_hdr_metadata(HdrStaticMetadata metadata) noexcept { hdr_ = metadata; }
  const HdrStaticMetadata& hdr_metadata() const noexcept { return hdr_; }

  Color to_working(Color encoded_input) const;
  Color from_working_to_display(Color working) const;
  Color from_working_to_output(Color working) const;
  ColorTransformResult transform(Color encoded_input) const;
  void transform_image(std::span<const Color> source,
                       std::span<Color> preview,
                       std::span<Color> output) const;

  DigitorResult generate_scopes(std::span<const Color> working_pixels,
                                std::uint32_t width,
                                std::uint32_t height,
                                const ScopeConfig& config,
                                ScopeResult& result,
                                std::string* diagnostic = nullptr) const;

  ColorPipelineTelemetry telemetry() const;
  static const char* name(ManagedColorSpace space) noexcept;
  static bool is_hdr(ManagedColorSpace space) noexcept;
  static bool is_scene_linear(ManagedColorSpace space) noexcept;

 private:
  Color decode_to_linear(Color value, ManagedColorSpace space) const;
  Color encode_from_linear(Color value, ManagedColorSpace space) const;
  Color convert_primaries(Color value, ManagedColorSpace source,
                          ManagedColorSpace destination) const;
  Color apply_gamut_map(Color value) const;
  Color apply_tone_map(Color value, ManagedColorSpace destination) const;
  DigitorResult generate_scopes_cpu(std::span<const Color> pixels,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    const ScopeConfig& config,
                                    ScopeResult& result,
                                    std::string& diagnostic) const;

  ColorManagementConfig config_;
  HdrStaticMetadata hdr_;
  ScopeBackendCallbacks scope_callbacks_;
  mutable ColorPipelineTelemetry telemetry_;
};

}  // namespace digitor
