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

enum class IccProfileClass { unknown, input, display, output, device_link, color_space, abstract_profile, named_color };
enum class IccColorSpace { unknown, rgb, gray, cmyk, xyz, lab };
enum class DisplayTransfer { unknown, srgb, gamma22, gamma24, pq, hlg, icc_trc };
enum class DisplayIntent { perceptual, relative_colorimetric, saturation, absolute_colorimetric };

struct IccProfileInfo {
  std::uint32_t declared_size{};
  std::uint32_t version_raw{};
  IccProfileClass profile_class{IccProfileClass::unknown};
  IccColorSpace data_color_space{IccColorSpace::unknown};
  IccColorSpace pcs{IccColorSpace::unknown};
  std::array<std::uint8_t, 16> profile_id{};
  std::string device_manufacturer;
  std::string device_model;
  std::uint32_t rendering_intent{};
  std::uint32_t tag_count{};
  bool signature_valid{};
  bool structurally_valid{};
};

struct DisplayDescriptor {
  std::string id;
  std::string name;
  std::string platform_handle;
  std::vector<std::byte> icc_profile;
  IccProfileInfo profile;
  double peak_nits{100.0};
  double black_nits{0.1};
  bool hdr_capable{};
  bool primary{};
  DisplayTransfer transfer{DisplayTransfer::icc_trc};
};

struct DisplayTransformRequest {
  std::string display_id;
  DisplayIntent intent{DisplayIntent::relative_colorimetric};
  bool black_point_compensation{true};
  bool preserve_alpha{true};
  bool require_icc_profile{};
};

struct DisplayTransformPlan {
  std::string display_id;
  IccProfileInfo profile;
  DisplayIntent intent{DisplayIntent::relative_colorimetric};
  DisplayTransfer transfer{DisplayTransfer::unknown};
  double peak_nits{100.0};
  double black_nits{0.1};
  bool use_icc{};
  bool hdr{};
  bool black_point_compensation{};
};

struct DisplayManagementTelemetry {
  std::uint64_t profile_parses{};
  std::uint64_t rejected_profiles{};
  std::uint64_t display_registrations{};
  std::uint64_t plan_builds{};
  std::uint64_t fallback_plans{};
  std::string active_display_id;
  std::string last_error;
};

class IccProfileParser {
 public:
  static DigitorResult parse(std::span<const std::byte> bytes, IccProfileInfo& info,
                             std::string* diagnostic = nullptr);
};

class DisplayManager {
 public:
  DigitorResult register_display(DisplayDescriptor display, std::string* diagnostic = nullptr);
  DigitorResult remove_display(const std::string& id, std::string* diagnostic = nullptr);
  DigitorResult set_active_display(const std::string& id, std::string* diagnostic = nullptr);
  const DisplayDescriptor* active_display() const noexcept;
  const DisplayDescriptor* find_display(const std::string& id) const noexcept;
  std::vector<DisplayDescriptor> displays() const;
  DigitorResult build_plan(const DisplayTransformRequest& request, DisplayTransformPlan& plan,
                           std::string* diagnostic = nullptr);
  DisplayManagementTelemetry telemetry() const noexcept { return telemetry_; }

 private:
  std::vector<DisplayDescriptor> displays_;
  std::string active_id_;
  DisplayManagementTelemetry telemetry_;
};

}  // namespace digitor
