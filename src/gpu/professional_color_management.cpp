#include "digitor/professional_color_management.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace digitor {
namespace {

constexpr double kPi = 3.14159265358979323846;

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
double clamp01d(double v) { return std::clamp(v, 0.0, 1.0); }
bool finite(Color c) {
  return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) &&
         std::isfinite(c.a);
}

color::Primaries primaries_for(ManagedColorSpace space) {
  switch (space) {
    case ManagedColorSpace::display_p3: return color::Primaries::display_p3;
    case ManagedColorSpace::rec2020_gamma24:
    case ManagedColorSpace::rec2020_pq:
    case ManagedColorSpace::rec2020_hlg:
    case ManagedColorSpace::linear_rec2020:
      return color::Primaries::bt2020;
    case ManagedColorSpace::aces_cg:
    case ManagedColorSpace::aces_cct:
      return color::Primaries::unspecified;
    default:
      return color::Primaries::bt709;
  }
}

color::Chromaticities chromaticities_for(ManagedColorSpace space) {
  if (space == ManagedColorSpace::aces_cg || space == ManagedColorSpace::aces_cct) {
    return {{0.713, 0.293}, {0.165, 0.830}, {0.128, 0.044}, {0.32168, 0.33767}};
  }
  if (space == ManagedColorSpace::sony_slog3_sgamut3cine)
    return {{0.766, 0.275}, {0.225, 0.800}, {0.089, -0.087}, {0.3127, 0.3290}};
  if (space == ManagedColorSpace::canon_log2_cinema_gamut)
    return {{0.740, 0.270}, {0.170, 1.140}, {0.080, -0.100}, {0.3127, 0.3290}};
  if (space == ManagedColorSpace::panasonic_vlog_vgamut)
    return {{0.730, 0.280}, {0.165, 0.840}, {0.100, -0.030}, {0.3127, 0.3290}};
  if (space == ManagedColorSpace::arri_logc3_wide_gamut)
    return {{0.684, 0.313}, {0.221, 0.848}, {0.0861, -0.102}, {0.3127, 0.3290}};
  if (space == ManagedColorSpace::blackmagic_film_gen5)
    return {{0.7177215, 0.3171181}, {0.2280410, 0.8615690}, {0.1005841, -0.0820452}, {0.3127, 0.3290}};
  return color::chromaticities(primaries_for(space));
}

float decode_log_channel(ManagedColorSpace space, float x) {
  const double v = x;
  switch (space) {
    case ManagedColorSpace::aces_cct:
      if (v <= 0.155251141552511) return static_cast<float>((v - 0.0729055341958355) / 10.5402377416545);
      return static_cast<float>(std::pow(2.0, v * 17.52 - 9.72));
    case ManagedColorSpace::sony_slog3_sgamut3cine:
      if (v >= 171.2102946929 / 1023.0)
        return static_cast<float>((std::pow(10.0, (v * 1023.0 - 420.0) / 261.5) * 0.19 - 0.01) / 0.18);
      return static_cast<float>((v * 1023.0 - 95.0) / 171.2102946929 * 0.01125);
    case ManagedColorSpace::canon_log2_cinema_gamut:
      return static_cast<float>((v < 0.092864125)
          ? -(std::pow(10.0, (0.092864125 - v) / 0.24136077) - 1.0) / 87.099375
          : (std::pow(10.0, (v - 0.092864125) / 0.24136077) - 1.0) / 87.099375);
    case ManagedColorSpace::panasonic_vlog_vgamut:
      if (v < 0.181) return static_cast<float>((v - 0.125) / 5.6);
      return static_cast<float>(std::pow(10.0, (v - 0.598206) / 0.241514) - 0.00873);
    case ManagedColorSpace::arri_logc3_wide_gamut:
      if (v > 0.1496582) return static_cast<float>((std::pow(10.0, (v - 0.385537) / 0.247190) - 0.052272) / 5.555556);
      return static_cast<float>((v - 0.092809) / 5.367655);
    case ManagedColorSpace::blackmagic_film_gen5:
      if (v >= 0.133883) return static_cast<float>((std::exp((v - 0.530013) / 0.0869288) - 0.00549407) / 8.28361);
      return static_cast<float>((v - 0.0924658) / 8.28361);
    default: return x;
  }
}

float encode_log_channel(ManagedColorSpace space, float x) {
  const double v = x;
  switch (space) {
    case ManagedColorSpace::aces_cct:
      if (v <= 0.0078125) return static_cast<float>(10.5402377416545 * v + 0.0729055341958355);
      return static_cast<float>((std::log2(std::max(v, 1e-12)) + 9.72) / 17.52);
    case ManagedColorSpace::sony_slog3_sgamut3cine:
      if (v >= 0.01125)
        return static_cast<float>((420.0 + std::log10((v * 0.18 + 0.01) / 0.19) * 261.5) / 1023.0);
      return static_cast<float>((v * 171.2102946929 / 0.01125 + 95.0) / 1023.0);
    case ManagedColorSpace::canon_log2_cinema_gamut:
      return static_cast<float>(v < 0.0
          ? 0.092864125 - 0.24136077 * std::log10(1.0 - 87.099375 * v)
          : 0.092864125 + 0.24136077 * std::log10(1.0 + 87.099375 * v));
    case ManagedColorSpace::panasonic_vlog_vgamut:
      if (v < 0.01) return static_cast<float>(5.6 * v + 0.125);
      return static_cast<float>(0.241514 * std::log10(v + 0.00873) + 0.598206);
    case ManagedColorSpace::arri_logc3_wide_gamut:
      if (v > 0.010591) return static_cast<float>(0.247190 * std::log10(5.555556 * v + 0.052272) + 0.385537);
      return static_cast<float>(5.367655 * v + 0.092809);
    case ManagedColorSpace::blackmagic_film_gen5:
      if (v >= 0.005) return static_cast<float>(0.0869288 * std::log(8.28361 * v + 0.00549407) + 0.530013);
      return static_cast<float>(8.28361 * v + 0.0924658);
    default: return x;
  }
}

color::Transfer transfer_for(ManagedColorSpace space) {
  switch (space) {
    case ManagedColorSpace::srgb:
    case ManagedColorSpace::display_p3: return color::Transfer::srgb;
    case ManagedColorSpace::rec2020_pq: return color::Transfer::pq;
    case ManagedColorSpace::rec2020_hlg: return color::Transfer::hlg;
    case ManagedColorSpace::linear_rec709:
    case ManagedColorSpace::linear_rec2020:
    case ManagedColorSpace::aces_cg: return color::Transfer::linear;
    default: return color::Transfer::gamma24;
  }
}

float luminance(Color c) {
  return std::max(0.0f, 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b);
}

std::size_t index_2d(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
  return static_cast<std::size_t>(y) * width + x;
}

}  // namespace

ProfessionalColorManagement::ProfessionalColorManagement(ColorManagementConfig config,
                                                         ScopeBackendCallbacks scopes)
    : config_(config), scope_callbacks_(std::move(scopes)) {
  set_config(config);
}

void ProfessionalColorManagement::set_config(ColorManagementConfig config) {
  if (config.reference_white_nits <= 0.0 || config.display_peak_nits <= 0.0 ||
      config.content_peak_nits <= 0.0)
    throw std::invalid_argument("color-management luminance values must be positive");
  config_ = config;
  telemetry_.input_transform = name(config_.input);
  telemetry_.working_space = name(config_.working);
  telemetry_.display_transform = name(config_.display);
  telemetry_.output_transform = name(config_.output);
}

const char* ProfessionalColorManagement::name(ManagedColorSpace space) noexcept {
  switch (space) {
    case ManagedColorSpace::rec709_gamma24: return "Rec.709 Gamma 2.4";
    case ManagedColorSpace::srgb: return "sRGB";
    case ManagedColorSpace::display_p3: return "Display P3";
    case ManagedColorSpace::rec2020_gamma24: return "Rec.2020 Gamma 2.4";
    case ManagedColorSpace::rec2020_pq: return "Rec.2020 PQ";
    case ManagedColorSpace::rec2020_hlg: return "Rec.2020 HLG";
    case ManagedColorSpace::linear_rec709: return "Linear Rec.709";
    case ManagedColorSpace::linear_rec2020: return "Linear Rec.2020";
    case ManagedColorSpace::aces_cg: return "ACEScg";
    case ManagedColorSpace::aces_cct: return "ACEScct";
    case ManagedColorSpace::sony_slog3_sgamut3cine: return "Sony S-Log3 S-Gamut3.Cine";
    case ManagedColorSpace::canon_log2_cinema_gamut: return "Canon Log 2 Cinema Gamut";
    case ManagedColorSpace::panasonic_vlog_vgamut: return "Panasonic V-Log V-Gamut";
    case ManagedColorSpace::arri_logc3_wide_gamut: return "ARRI LogC3 Wide Gamut";
    case ManagedColorSpace::blackmagic_film_gen5: return "Blackmagic Film Gen 5";
    default: return "Unknown";
  }
}

bool ProfessionalColorManagement::is_hdr(ManagedColorSpace space) noexcept {
  return space == ManagedColorSpace::rec2020_pq || space == ManagedColorSpace::rec2020_hlg;
}

bool ProfessionalColorManagement::is_scene_linear(ManagedColorSpace space) noexcept {
  return space == ManagedColorSpace::linear_rec709 ||
         space == ManagedColorSpace::linear_rec2020 || space == ManagedColorSpace::aces_cg;
}

Color ProfessionalColorManagement::decode_to_linear(Color value, ManagedColorSpace space) const {
  if (!finite(value)) {
    ++telemetry_.non_finite_pixels;
    return {};
  }
  const bool custom_log = space == ManagedColorSpace::aces_cct ||
      space == ManagedColorSpace::sony_slog3_sgamut3cine ||
      space == ManagedColorSpace::canon_log2_cinema_gamut ||
      space == ManagedColorSpace::panasonic_vlog_vgamut ||
      space == ManagedColorSpace::arri_logc3_wide_gamut ||
      space == ManagedColorSpace::blackmagic_film_gen5;
  if (custom_log) {
    value.r = decode_log_channel(space, value.r);
    value.g = decode_log_channel(space, value.g);
    value.b = decode_log_channel(space, value.b);
  } else {
    const auto transfer = transfer_for(space);
    value.r = color::decode(transfer, std::max(0.0f, value.r));
    value.g = color::decode(transfer, std::max(0.0f, value.g));
    value.b = color::decode(transfer, std::max(0.0f, value.b));
  }
  return value;
}

Color ProfessionalColorManagement::encode_from_linear(Color value, ManagedColorSpace space) const {
  const bool custom_log = space == ManagedColorSpace::aces_cct ||
      space == ManagedColorSpace::sony_slog3_sgamut3cine ||
      space == ManagedColorSpace::canon_log2_cinema_gamut ||
      space == ManagedColorSpace::panasonic_vlog_vgamut ||
      space == ManagedColorSpace::arri_logc3_wide_gamut ||
      space == ManagedColorSpace::blackmagic_film_gen5;
  if (custom_log) {
    value.r = encode_log_channel(space, value.r);
    value.g = encode_log_channel(space, value.g);
    value.b = encode_log_channel(space, value.b);
  } else {
    const auto transfer = transfer_for(space);
    value.r = color::encode(transfer, std::max(0.0f, value.r));
    value.g = color::encode(transfer, std::max(0.0f, value.g));
    value.b = color::encode(transfer, std::max(0.0f, value.b));
  }
  return value;
}

Color ProfessionalColorManagement::convert_primaries(Color value, ManagedColorSpace source,
                                                      ManagedColorSpace destination) const {
  if (source == destination) return value;
  const auto src = chromaticities_for(source);
  const auto dst = chromaticities_for(destination);
  auto matrix = color::rgb_to_rgb(src, dst);
  if (src.white.x != dst.white.x || src.white.y != dst.white.y) {
    const auto adapt = color::adaptation_matrix(color::Adaptation::bradford, src.white, dst.white);
    matrix = color::multiply(color::multiply(color::inverse(color::rgb_to_xyz(dst)), adapt),
                             color::rgb_to_xyz(src));
  }
  const auto converted = color::multiply(matrix, {value.r, value.g, value.b});
  value.r = static_cast<float>(converted.x);
  value.g = static_cast<float>(converted.y);
  value.b = static_cast<float>(converted.z);
  return value;
}

Color ProfessionalColorManagement::apply_gamut_map(Color value) const {
  if (config_.gamut_map == GamutMapMode::none) return value;
  const float low = std::min({value.r, value.g, value.b});
  const float high = std::max({value.r, value.g, value.b});
  if (low >= 0.0f && high <= 1.0f) return value;
  if (config_.gamut_map == GamutMapMode::clip) {
    ++telemetry_.clipped_pixels;
    value.r = clamp01(value.r); value.g = clamp01(value.g); value.b = clamp01(value.b);
    return value;
  }
  ++telemetry_.gamut_compressions;
  const float y = luminance(value);
  const float chroma_scale = high > 1.0f ? 1.0f / (1.0f + (high - 1.0f)) : 1.0f;
  value.r = y + (value.r - y) * chroma_scale;
  value.g = y + (value.g - y) * chroma_scale;
  value.b = y + (value.b - y) * chroma_scale;
  const float min_after = std::min({value.r, value.g, value.b});
  if (min_after < 0.0f) {
    const float lift = -min_after;
    value.r += lift; value.g += lift; value.b += lift;
  }
  return value;
}

Color ProfessionalColorManagement::apply_tone_map(Color value,
                                                   ManagedColorSpace destination) const {
  if (config_.tone_map == ToneMapMode::none || is_hdr(destination)) return value;
  const double content_peak = hdr_.mastering_present && hdr_.max_luminance_nits > 0.0
      ? hdr_.max_luminance_nits : config_.content_peak_nits;
  if (content_peak <= config_.display_peak_nits) return value;
  ++telemetry_.tone_mapped_pixels;
  auto map = [this](float x) {
    const double nits = std::max(0.0, static_cast<double>(x)) * config_.reference_white_nits;
    const double normalized = nits / config_.display_peak_nits;
    double out = normalized;
    switch (config_.tone_map) {
      case ToneMapMode::reinhard: out = normalized / (1.0 + normalized); break;
      case ToneMapMode::hable: {
        const double A=.15, B=.50, C=.10, D=.20, E=.02, F=.30;
        out = ((normalized*(A*normalized+C*B)+D*E)/(normalized*(A*normalized+B)+D*F))-E/F;
        break;
      }
      case ToneMapMode::bt2390_like: {
        const double knee = 0.75;
        out = normalized <= knee ? normalized
            : knee + (1.0 - knee) * (1.0 - std::exp(-(normalized - knee)/(1.0-knee)));
        break;
      }
      default: break;
    }
    return static_cast<float>(std::max(0.0, out) * config_.display_peak_nits /
                              config_.reference_white_nits);
  };
  value.r = map(value.r); value.g = map(value.g); value.b = map(value.b);
  return value;
}

Color ProfessionalColorManagement::to_working(Color encoded_input) const {
  auto linear = decode_to_linear(encoded_input, config_.input);
  linear = convert_primaries(linear, config_.input, config_.working);
  if (!config_.preserve_negative_values) {
    linear.r = std::max(0.0f, linear.r); linear.g = std::max(0.0f, linear.g);
    linear.b = std::max(0.0f, linear.b);
  }
  return is_scene_linear(config_.working) ? linear : encode_from_linear(linear, config_.working);
}

Color ProfessionalColorManagement::from_working_to_display(Color working) const {
  auto linear = is_scene_linear(config_.working) ? working : decode_to_linear(working, config_.working);
  linear = apply_tone_map(linear, config_.display);
  linear = convert_primaries(linear, config_.working, config_.display);
  linear = apply_gamut_map(linear);
  return config_.enable_display_transform ? encode_from_linear(linear, config_.display) : linear;
}

Color ProfessionalColorManagement::from_working_to_output(Color working) const {
  auto linear = is_scene_linear(config_.working) ? working : decode_to_linear(working, config_.working);
  linear = apply_tone_map(linear, config_.output);
  linear = convert_primaries(linear, config_.working, config_.output);
  linear = apply_gamut_map(linear);
  auto encoded = config_.enable_output_transform ? encode_from_linear(linear, config_.output) : linear;
  if (config_.legal_range_output) {
    encoded.r = 16.0f/255.0f + clamp01(encoded.r) * 219.0f/255.0f;
    encoded.g = 16.0f/255.0f + clamp01(encoded.g) * 219.0f/255.0f;
    encoded.b = 16.0f/255.0f + clamp01(encoded.b) * 219.0f/255.0f;
  }
  return encoded;
}

ColorTransformResult ProfessionalColorManagement::transform(Color encoded_input) const {
  const auto working = to_working(encoded_input);
  ++telemetry_.transformed_pixels;
  return {from_working_to_display(working), from_working_to_output(working)};
}

void ProfessionalColorManagement::transform_image(std::span<const Color> source,
                                                   std::span<Color> preview,
                                                   std::span<Color> output) const {
  if (source.size() != preview.size() || source.size() != output.size())
    throw std::invalid_argument("color transform image spans must have equal size");
  for (std::size_t i = 0; i < source.size(); ++i) {
    const auto result = transform(source[i]);
    preview[i] = result.preview;
    output[i] = result.output;
  }
}

DigitorResult ProfessionalColorManagement::generate_scopes(
    std::span<const Color> working_pixels, std::uint32_t width, std::uint32_t height,
    const ScopeConfig& config, ScopeResult& result, std::string* diagnostic) const {
  if (width == 0 || height == 0 || working_pixels.size() != static_cast<std::size_t>(width) * height ||
      config.waveform_width == 0 || config.waveform_height == 0 ||
      config.vectorscope_size == 0 || config.histogram_bins == 0 || config.sample_step == 0) {
    if (diagnostic) *diagnostic = "invalid scope dimensions or source size";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  std::string local;
  if (scope_callbacks_.dispatch_gpu) {
    const auto gpu = scope_callbacks_.dispatch_gpu(working_pixels, width, height, config, result, local);
    if (gpu == DIGITOR_RESULT_OK) {
      ++telemetry_.scope_frames;
      if (diagnostic) diagnostic->clear();
      return gpu;
    }
  }
  const auto cpu = generate_scopes_cpu(working_pixels, width, height, config, result, local);
  if (cpu == DIGITOR_RESULT_OK) ++telemetry_.scope_frames;
  if (diagnostic) *diagnostic = local;
  return cpu;
}

DigitorResult ProfessionalColorManagement::generate_scopes_cpu(
    std::span<const Color> pixels, std::uint32_t width, std::uint32_t height,
    const ScopeConfig& config, ScopeResult& out, std::string& diagnostic) const {
  try {
    out = {};
    out.source_width = width; out.source_height = height;
    const std::size_t waveform_size = static_cast<std::size_t>(config.waveform_width) * config.waveform_height;
    const std::size_t vector_size = static_cast<std::size_t>(config.vectorscope_size) * config.vectorscope_size;
    out.waveform_luma.assign(waveform_size, 0);
    for (auto& plane : out.waveform_rgb) plane.assign(waveform_size, 0);
    for (auto& plane : out.parade) plane.assign(waveform_size, 0);
    out.vectorscope.assign(vector_size, 0);
    for (auto& histogram : out.histogram_rgb) histogram.assign(config.histogram_bins, 0);
    out.histogram_luma.assign(config.histogram_bins, 0);
    out.cie_xy.assign(vector_size, 0);
    out.false_color_rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);

    double sum_nits = 0.0;
    const auto bin = [config](float v) {
      return std::min(config.histogram_bins - 1,
          static_cast<std::uint32_t>(clamp01(v) * (config.histogram_bins - 1)));
    };
    for (std::uint32_t y = 0; y < height; y += config.sample_step) {
      for (std::uint32_t x = 0; x < width; x += config.sample_step) {
        const Color c = pixels[index_2d(x, y, width)];
        if (!finite(c)) continue;
        ++out.sampled_pixels;
        const float luma = luminance(c);
        const double nits = luma * config_.reference_white_nits;
        sum_nits += nits; out.peak_nits = std::max(out.peak_nits, nits);

        const std::uint32_t wx = std::min(config.waveform_width - 1,
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * config.waveform_width) / width));
        const auto wy = [config](float v) {
          return config.waveform_height - 1 - std::min(config.waveform_height - 1,
              static_cast<std::uint32_t>(clamp01(v) * (config.waveform_height - 1)));
        };
        ++out.waveform_luma[index_2d(wx, wy(luma), config.waveform_width)];
        const float channels[3]{c.r, c.g, c.b};
        for (int channel = 0; channel < 3; ++channel) {
          ++out.waveform_rgb[channel][index_2d(wx, wy(channels[channel]), config.waveform_width)];
          const std::uint32_t section = config.waveform_width / 3;
          const std::uint32_t px = std::min(config.waveform_width - 1,
              static_cast<std::uint32_t>(channel) * section +
              static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * std::max(1u, section)) / width));
          ++out.parade[channel][index_2d(px, wy(channels[channel]), config.waveform_width)];
          ++out.histogram_rgb[channel][bin(channels[channel])];
        }
        ++out.histogram_luma[bin(luma)];

        const double cb = (c.b - luma) / 1.8556;
        const double cr = (c.r - luma) / 1.5748;
        const std::uint32_t vx = std::min(config.vectorscope_size - 1,
            static_cast<std::uint32_t>(clamp01d(0.5 + cb) * (config.vectorscope_size - 1)));
        const std::uint32_t vy = std::min(config.vectorscope_size - 1,
            static_cast<std::uint32_t>(clamp01d(0.5 - cr) * (config.vectorscope_size - 1)));
        ++out.vectorscope[index_2d(vx, vy, config.vectorscope_size)];

        const auto xyz = color::multiply(color::rgb_to_xyz(chromaticities_for(config_.working)),
                                         {c.r, c.g, c.b});
        const double total = xyz.x + xyz.y + xyz.z;
        if (total > 1e-12) {
          const std::uint32_t cx = std::min(config.vectorscope_size - 1,
              static_cast<std::uint32_t>(clamp01d(xyz.x / total) * (config.vectorscope_size - 1)));
          const std::uint32_t cy = std::min(config.vectorscope_size - 1,
              static_cast<std::uint32_t>((1.0 - clamp01d(xyz.y / total)) * (config.vectorscope_size - 1)));
          ++out.cie_xy[index_2d(cx, cy, config.vectorscope_size)];
        }

        const std::size_t fi = index_2d(x, y, width) * 4;
        const double scale = config.hdr_scale ? std::max(1.0, config.max_nits) : 100.0;
        const double level = nits / scale;
        std::array<std::uint8_t, 4> fc{0, 0, 255, 255};
        if (level >= 1.0) fc = {255, 0, 255, 255};
        else if (level >= .75) fc = {255, 0, 0, 255};
        else if (level >= .50) fc = {255, 255, 0, 255};
        else if (level >= .25) fc = {0, 255, 0, 255};
        else if (level >= .10) fc = {0, 255, 255, 255};
        std::copy(fc.begin(), fc.end(), out.false_color_rgba.begin() + static_cast<std::ptrdiff_t>(fi));
      }
    }
    out.average_nits = out.sampled_pixels ? sum_nits / static_cast<double>(out.sampled_pixels) : 0.0;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  } catch (const std::bad_alloc&) {
    diagnostic = "scope allocation failed";
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (const std::exception& error) {
    diagnostic = error.what();
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

ColorPipelineTelemetry ProfessionalColorManagement::telemetry() const { return telemetry_; }

}  // namespace digitor
