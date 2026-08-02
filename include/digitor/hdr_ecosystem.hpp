#pragma once

#include "digitor/color.hpp"
#include "digitor/digitor.h"
#include "digitor/professional_color_management.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace digitor {

enum class HdrStandard { sdr, hdr10, hlg, hdr10_plus, dolby_vision };
enum class HdrMetadataTransport { none, static_sei, dynamic_sei, container_side_data, rpu };
enum class HdrToneMapOperator { none, bt2390, perceptual, scene_adaptive, display_adaptive };
enum class HdrTrimTarget { nits_100, nits_600, nits_1000, nits_2000, nits_4000 };

struct Hdr10PlusWindow {
  double maxscl_r{};
  double maxscl_g{};
  double maxscl_b{};
  double average_maxrgb{};
  std::vector<double> distribution_percentiles;
  std::vector<double> distribution_values;
  double knee_x{};
  double knee_y{};
  std::vector<double> bezier_anchors;
};

struct Hdr10PlusMetadata {
  std::uint64_t frame_index{};
  std::int64_t timestamp_us{};
  std::vector<Hdr10PlusWindow> windows;
  bool scene_refresh{};
};

struct DolbyVisionTrim {
  HdrTrimTarget target{HdrTrimTarget::nits_100};
  double slope{1.0};
  double offset{};
  double power{1.0};
  double saturation{1.0};
};

struct DolbyVisionMetadata {
  std::uint64_t frame_index{};
  std::int64_t timestamp_us{};
  int profile{8};
  int level{6};
  bool enhancement_layer{};
  std::vector<DolbyVisionTrim> trims;
  std::vector<std::byte> vendor_payload;
};

struct HdrFrameMetadata {
  HdrStandard standard{HdrStandard::sdr};
  HdrStaticMetadata static_metadata;
  std::optional<Hdr10PlusMetadata> hdr10_plus;
  std::optional<DolbyVisionMetadata> dolby_vision;
};

struct HdrDisplayTarget {
  HdrStandard standard{HdrStandard::sdr};
  double peak_nits{100.0};
  double black_nits{0.1};
  double reference_white_nits{100.0};
  bool supports_dynamic_metadata{};
};

struct AdvancedToneMapConfig {
  HdrToneMapOperator operation{HdrToneMapOperator::display_adaptive};
  double source_peak_nits{1000.0};
  double target_peak_nits{100.0};
  double reference_white_nits{100.0};
  double highlight_rolloff{0.6};
  double shadow_lift{};
  double saturation_compensation{1.0};
  bool preserve_hue{true};
  bool preserve_skin_tones{true};
};

struct HdrAnalysisResult {
  double max_rgb_nits{};
  double average_rgb_nits{};
  double max_cll{};
  double max_fall{};
  double scene_cut_score{};
  std::vector<double> percentile_nits;
};

struct HdrPacket {
  HdrMetadataTransport transport{HdrMetadataTransport::none};
  HdrStandard standard{HdrStandard::sdr};
  std::int64_t timestamp_us{};
  std::vector<std::byte> payload;
};

struct HdrAdapterCallbacks {
  std::function<DigitorResult(const DolbyVisionMetadata&, HdrPacket&, std::string&)> package_dolby_vision;
  std::function<DigitorResult(const Hdr10PlusMetadata&, HdrPacket&, std::string&)> package_hdr10_plus;
};

struct HdrEcosystemTelemetry {
  std::uint64_t analyzed_frames{};
  std::uint64_t tone_mapped_pixels{};
  std::uint64_t dynamic_metadata_frames{};
  std::uint64_t rejected_metadata{};
  std::uint64_t scene_changes{};
  std::string last_error;
};

class HdrEcosystem {
 public:
  explicit HdrEcosystem(HdrAdapterCallbacks callbacks = {});

  DigitorResult validate(const HdrFrameMetadata& metadata,
                         std::string* diagnostic = nullptr) const;
  DigitorResult analyze(std::span<const Color> linear_pixels,
                        double reference_white_nits,
                        HdrAnalysisResult& result,
                        std::string* diagnostic = nullptr);
  DigitorResult tone_map(std::span<const Color> source,
                         std::span<Color> destination,
                         const AdvancedToneMapConfig& config,
                         std::string* diagnostic = nullptr);
  DigitorResult generate_hdr10_plus(std::uint64_t frame_index,
                                    std::int64_t timestamp_us,
                                    const HdrAnalysisResult& analysis,
                                    bool scene_refresh,
                                    Hdr10PlusMetadata& metadata,
                                    std::string* diagnostic = nullptr);
  DigitorResult package_metadata(const HdrFrameMetadata& metadata,
                                 HdrPacket& packet,
                                 std::string* diagnostic = nullptr);
  HdrEcosystemTelemetry telemetry() const;

 private:
  HdrAdapterCallbacks callbacks_;
  HdrEcosystemTelemetry telemetry_;
};

}  // namespace digitor
