#include "digitor/icc_display_management.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

void put32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  bytes[offset] = static_cast<std::byte>((value >> 24u) & 0xffu);
  bytes[offset + 1u] = static_cast<std::byte>((value >> 16u) & 0xffu);
  bytes[offset + 2u] = static_cast<std::byte>((value >> 8u) & 0xffu);
  bytes[offset + 3u] = static_cast<std::byte>(value & 0xffu);
}

void put4(std::vector<std::byte>& bytes, std::size_t offset, const char* value) {
  for (std::size_t i = 0; i < 4u; ++i) bytes[offset + i] = static_cast<std::byte>(value[i]);
}

std::vector<std::byte> display_profile() {
  std::vector<std::byte> bytes(132u);
  put32(bytes, 0u, static_cast<std::uint32_t>(bytes.size()));
  put32(bytes, 8u, 0x04300000u);
  put4(bytes, 12u, "mntr");
  put4(bytes, 16u, "RGB ");
  put4(bytes, 20u, "XYZ ");
  put4(bytes, 36u, "acsp");
  put4(bytes, 48u, "DGTR");
  put4(bytes, 52u, "MON1");
  put32(bytes, 64u, 1u);
  put32(bytes, 128u, 0u);
  return bytes;
}

}  // namespace

int main() {
  using namespace digitor;

  IccProfileInfo profile;
  std::string diagnostic;
  auto bytes = display_profile();
  assert(IccProfileParser::parse(bytes, profile, &diagnostic) == DIGITOR_RESULT_OK);
  assert(profile.structurally_valid);
  assert(profile.profile_class == IccProfileClass::display);
  assert(profile.data_color_space == IccColorSpace::rgb);
  assert(profile.pcs == IccColorSpace::xyz);

  auto malformed = bytes;
  put4(malformed, 36u, "nope");
  assert(IccProfileParser::parse(malformed, profile, &diagnostic) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(!diagnostic.empty());

  DisplayManager manager;
  DisplayDescriptor primary;
  primary.id = "display-1";
  primary.name = "Reference display";
  primary.icc_profile = bytes;
  primary.primary = true;
  primary.peak_nits = 120.0;
  assert(manager.register_display(primary, &diagnostic) == DIGITOR_RESULT_OK);
  assert(manager.active_display() != nullptr);
  assert(manager.active_display()->id == "display-1");

  DisplayDescriptor hdr;
  hdr.id = "display-2";
  hdr.name = "HDR display";
  hdr.hdr_capable = true;
  hdr.transfer = DisplayTransfer::pq;
  hdr.peak_nits = 1000.0;
  assert(manager.register_display(hdr, &diagnostic) == DIGITOR_RESULT_OK);
  assert(manager.set_active_display("display-2", &diagnostic) == DIGITOR_RESULT_OK);

  DisplayTransformPlan plan;
  DisplayTransformRequest request;
  assert(manager.build_plan(request, plan, &diagnostic) == DIGITOR_RESULT_OK);
  assert(plan.display_id == "display-2");
  assert(plan.hdr);
  assert(!plan.use_icc);

  request.require_icc_profile = true;
  assert(manager.build_plan(request, plan, &diagnostic) ==
         DIGITOR_RESULT_BACKEND_UNAVAILABLE);

  request.display_id = "display-1";
  assert(manager.build_plan(request, plan, &diagnostic) == DIGITOR_RESULT_OK);
  assert(plan.use_icc);
  assert(plan.profile.profile_class == IccProfileClass::display);

  const auto telemetry = manager.telemetry();
  assert(telemetry.display_registrations == 2u);
  assert(telemetry.profile_parses == 1u);
  assert(telemetry.plan_builds == 2u);
  assert(telemetry.fallback_plans == 1u);

  return 0;
}
