#pragma once

#include "digitor/color.hpp"
#include "digitor/digitor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace digitor {

enum class BroadcastStandard { rec601, rec709, rec2020_sdr, rec2020_pq, rec2020_hlg };
enum class BroadcastRange { full, legal };
enum class SafeAreaKind { action_safe, title_safe, custom };
enum class MatchMethod { neutral_balance, statistics, chart_24_patch, reference_frame };

struct SafeAreaConfig {
  SafeAreaKind kind{SafeAreaKind::action_safe};
  double left{0.05};
  double right{0.05};
  double top{0.05};
  double bottom{0.05};
};

struct BroadcastMonitorConfig {
  BroadcastStandard standard{BroadcastStandard::rec709};
  BroadcastRange range{BroadcastRange::legal};
  double reference_white_nits{100.0};
  double peak_nits{100.0};
  bool detect_gamut_excursions{true};
  bool detect_luma_excursions{true};
  bool detect_chroma_excursions{true};
  bool generate_warning_mask{true};
  SafeAreaConfig safe_area{};
};

struct BroadcastViolationCounts {
  std::uint64_t below_black{};
  std::uint64_t above_white{};
  std::uint64_t chroma_illegal{};
  std::uint64_t gamut_illegal{};
  std::uint64_t non_finite{};
};

struct BroadcastMonitorResult {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t analyzed_pixels{};
  BroadcastViolationCounts violations{};
  double minimum_luma{};
  double maximum_luma{};
  double average_luma{};
  double peak_nits{};
  bool broadcast_safe{};
  std::vector<std::uint8_t> warning_mask;
};

struct ColorPatch {
  Color measured{};
  Color reference{};
  double weight{1.0};
};

struct ColorMatchRequest {
  MatchMethod method{MatchMethod::statistics};
  std::span<const Color> source;
  std::span<const Color> reference;
  std::span<const ColorPatch> chart_patches;
  bool preserve_luminance{};
  bool preserve_skin_hues{true};
  double strength{1.0};
};

struct ColorMatchTransform {
  std::array<double, 9> matrix{1.0, 0.0, 0.0,
                               0.0, 1.0, 0.0,
                               0.0, 0.0, 1.0};
  std::array<double, 3> offset{};
  std::array<double, 3> gain{1.0, 1.0, 1.0};
  double confidence{};
  double mean_error_before{};
  double mean_error_after{};
};

struct BroadcastColorTelemetry {
  std::uint64_t monitored_frames{};
  std::uint64_t monitored_pixels{};
  std::uint64_t unsafe_frames{};
  std::uint64_t match_requests{};
  std::uint64_t matched_pixels{};
  std::uint64_t rejected_requests{};
  std::string last_error;
};

class BroadcastColorSuite {
 public:
  DigitorResult monitor(std::span<const Color> pixels,
                        std::uint32_t width,
                        std::uint32_t height,
                        const BroadcastMonitorConfig& config,
                        BroadcastMonitorResult& result,
                        std::string* diagnostic = nullptr);

  DigitorResult build_match(const ColorMatchRequest& request,
                            ColorMatchTransform& transform,
                            std::string* diagnostic = nullptr);

  DigitorResult apply_match(std::span<const Color> source,
                            std::span<Color> destination,
                            const ColorMatchTransform& transform,
                            double strength = 1.0,
                            std::string* diagnostic = nullptr);

  BroadcastColorTelemetry telemetry() const { return telemetry_; }

 private:
  BroadcastColorTelemetry telemetry_;
};

}  // namespace digitor
