#include "digitor/icc_display_management.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace digitor {
namespace {

std::uint32_t be32(std::span<const std::byte> bytes, std::size_t offset) {
  const auto* p = reinterpret_cast<const unsigned char*>(bytes.data() + offset);
  return (static_cast<std::uint32_t>(p[0]) << 24u) |
         (static_cast<std::uint32_t>(p[1]) << 16u) |
         (static_cast<std::uint32_t>(p[2]) << 8u) |
         static_cast<std::uint32_t>(p[3]);
}

std::string signature(std::span<const std::byte> bytes, std::size_t offset) {
  return std::string(reinterpret_cast<const char*>(bytes.data() + offset), 4u);
}

IccProfileClass profile_class(const std::string& value) {
  if (value == "scnr") return IccProfileClass::input;
  if (value == "mntr") return IccProfileClass::display;
  if (value == "prtr") return IccProfileClass::output;
  if (value == "link") return IccProfileClass::device_link;
  if (value == "spac") return IccProfileClass::color_space;
  if (value == "abst") return IccProfileClass::abstract_profile;
  if (value == "nmcl") return IccProfileClass::named_color;
  return IccProfileClass::unknown;
}

IccColorSpace color_space(const std::string& value) {
  if (value == "RGB ") return IccColorSpace::rgb;
  if (value == "GRAY") return IccColorSpace::gray;
  if (value == "CMYK") return IccColorSpace::cmyk;
  if (value == "XYZ ") return IccColorSpace::xyz;
  if (value == "Lab ") return IccColorSpace::lab;
  return IccColorSpace::unknown;
}

void set_diagnostic(std::string* diagnostic, std::string value) {
  if (diagnostic) *diagnostic = std::move(value);
}

}  // namespace

DigitorResult IccProfileParser::parse(std::span<const std::byte> bytes,
                                      IccProfileInfo& info,
                                      std::string* diagnostic) {
  info = {};
  if (bytes.size() < 132u) {
    set_diagnostic(diagnostic, "ICC profile is smaller than the 132-byte header and tag-count boundary");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  info.declared_size = be32(bytes, 0u);
  info.version_raw = be32(bytes, 8u);
  info.profile_class = profile_class(signature(bytes, 12u));
  info.data_color_space = color_space(signature(bytes, 16u));
  info.pcs = color_space(signature(bytes, 20u));
  info.device_manufacturer = signature(bytes, 48u);
  info.device_model = signature(bytes, 52u);
  info.rendering_intent = be32(bytes, 64u);
  info.signature_valid = signature(bytes, 36u) == "acsp";
  info.tag_count = be32(bytes, 128u);
  std::memcpy(info.profile_id.data(), bytes.data() + 84u, info.profile_id.size());

  if (!info.signature_valid) {
    set_diagnostic(diagnostic, "ICC profile does not contain the required acsp signature");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (info.declared_size < 132u || info.declared_size > bytes.size()) {
    set_diagnostic(diagnostic, "ICC declared profile size is outside the supplied byte span");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  const std::uint64_t tag_table_end = 132ull + static_cast<std::uint64_t>(info.tag_count) * 12ull;
  if (tag_table_end > info.declared_size) {
    set_diagnostic(diagnostic, "ICC tag table exceeds the declared profile size");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  for (std::uint32_t index = 0; index < info.tag_count; ++index) {
    const std::size_t entry = 132u + static_cast<std::size_t>(index) * 12u;
    const std::uint32_t offset = be32(bytes, entry + 4u);
    const std::uint32_t size = be32(bytes, entry + 8u);
    if (offset > info.declared_size || size > info.declared_size - offset) {
      set_diagnostic(diagnostic, "ICC tag payload exceeds the declared profile size");
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
  }
  if (info.profile_class == IccProfileClass::unknown ||
      info.data_color_space == IccColorSpace::unknown ||
      info.pcs == IccColorSpace::unknown) {
    set_diagnostic(diagnostic, "ICC profile contains an unsupported class or color-space signature");
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  info.structurally_valid = true;
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult DisplayManager::register_display(DisplayDescriptor display,
                                                std::string* diagnostic) {
  if (display.id.empty() || display.name.empty()) {
    telemetry_.last_error = "display id and name must not be empty";
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (!display.icc_profile.empty()) {
    ++telemetry_.profile_parses;
    std::string error;
    const auto result = IccProfileParser::parse(display.icc_profile, display.profile, &error);
    if (result != DIGITOR_RESULT_OK) {
      ++telemetry_.rejected_profiles;
      telemetry_.last_error = error;
      set_diagnostic(diagnostic, error);
      return result;
    }
    if (display.profile.profile_class != IccProfileClass::display) {
      telemetry_.last_error = "registered monitor profile must be an ICC display-class profile";
      set_diagnostic(diagnostic, telemetry_.last_error);
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
  }
  auto found = std::find_if(displays_.begin(), displays_.end(), [&](const auto& value) {
    return value.id == display.id;
  });
  if (found == displays_.end()) displays_.push_back(std::move(display));
  else *found = std::move(display);
  ++telemetry_.display_registrations;
  if (active_id_.empty() || displays_.back().primary) active_id_ = displays_.back().id;
  telemetry_.active_display_id = active_id_;
  telemetry_.last_error.clear();
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult DisplayManager::remove_display(const std::string& id,
                                              std::string* diagnostic) {
  const auto old_size = displays_.size();
  displays_.erase(std::remove_if(displays_.begin(), displays_.end(), [&](const auto& value) {
                    return value.id == id;
                  }),
                  displays_.end());
  if (old_size == displays_.size()) {
    set_diagnostic(diagnostic, "display was not registered");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (active_id_ == id) active_id_ = displays_.empty() ? std::string{} : displays_.front().id;
  telemetry_.active_display_id = active_id_;
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

DigitorResult DisplayManager::set_active_display(const std::string& id,
                                                  std::string* diagnostic) {
  if (!find_display(id)) {
    set_diagnostic(diagnostic, "display was not registered");
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  active_id_ = id;
  telemetry_.active_display_id = id;
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

const DisplayDescriptor* DisplayManager::active_display() const noexcept {
  return find_display(active_id_);
}

const DisplayDescriptor* DisplayManager::find_display(const std::string& id) const noexcept {
  const auto found = std::find_if(displays_.begin(), displays_.end(), [&](const auto& value) {
    return value.id == id;
  });
  return found == displays_.end() ? nullptr : &*found;
}

std::vector<DisplayDescriptor> DisplayManager::displays() const { return displays_; }

DigitorResult DisplayManager::build_plan(const DisplayTransformRequest& request,
                                         DisplayTransformPlan& plan,
                                         std::string* diagnostic) {
  const DisplayDescriptor* display = request.display_id.empty() ? active_display()
                                                                 : find_display(request.display_id);
  if (!display) {
    telemetry_.last_error = "no matching display is registered";
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (request.require_icc_profile && display->icc_profile.empty()) {
    telemetry_.last_error = "the selected display has no ICC profile";
    set_diagnostic(diagnostic, telemetry_.last_error);
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  plan = {};
  plan.display_id = display->id;
  plan.profile = display->profile;
  plan.intent = request.intent;
  plan.transfer = display->transfer;
  plan.peak_nits = display->peak_nits;
  plan.black_nits = display->black_nits;
  plan.use_icc = !display->icc_profile.empty();
  plan.hdr = display->hdr_capable;
  plan.black_point_compensation = request.black_point_compensation;
  ++telemetry_.plan_builds;
  if (!plan.use_icc) ++telemetry_.fallback_plans;
  telemetry_.last_error.clear();
  set_diagnostic(diagnostic, {});
  return DIGITOR_RESULT_OK;
}

}  // namespace digitor
