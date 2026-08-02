#include "digitor/hdr_ecosystem.hpp"

#include <cassert>
#include <cmath>
#include <string>
#include <vector>

using namespace digitor;

int main() {
  HdrEcosystem hdr;
  std::string diagnostic;

  std::vector<Color> pixels{{1.0f, 0.5f, 0.2f, 1.0f},
                            {4.0f, 2.0f, 1.0f, 0.75f},
                            {0.1f, 0.1f, 0.1f, 1.0f}};
  HdrAnalysisResult analysis;
  assert(hdr.analyze(pixels, 203.0, analysis, &diagnostic) == DIGITOR_RESULT_OK);
  assert(analysis.max_rgb_nits > analysis.average_rgb_nits);
  assert(analysis.percentile_nits.size() == 7u);

  std::vector<Color> mapped_pixels(pixels.size());
  AdvancedToneMapConfig tone;
  tone.source_peak_nits = 1000.0;
  tone.target_peak_nits = 100.0;
  tone.reference_white_nits = 203.0;
  assert(hdr.tone_map(pixels, mapped_pixels, tone, &diagnostic) == DIGITOR_RESULT_OK);
  assert(mapped_pixels[1].a == pixels[1].a);
  assert(mapped_pixels[1].r < pixels[1].r);

  Hdr10PlusMetadata hdr10plus;
  assert(hdr.generate_hdr10_plus(7, 233333, analysis, true, hdr10plus, &diagnostic) == DIGITOR_RESULT_OK);
  assert(hdr10plus.windows.size() == 1u);
  HdrFrameMetadata frame;
  frame.standard = HdrStandard::hdr10_plus;
  frame.hdr10_plus = hdr10plus;
  assert(hdr.validate(frame, &diagnostic) == DIGITOR_RESULT_OK);
  HdrPacket packet;
  assert(hdr.package_metadata(frame, packet, &diagnostic) == DIGITOR_RESULT_OK);
  assert(packet.transport == HdrMetadataTransport::dynamic_sei);
  assert(!packet.payload.empty());

  HdrFrameMetadata invalid;
  invalid.standard = HdrStandard::hdr10_plus;
  assert(hdr.validate(invalid, &diagnostic) == DIGITOR_RESULT_INVALID_ARGUMENT);

  DolbyVisionMetadata dv;
  dv.profile = 8;
  dv.level = 6;
  dv.trims.push_back({HdrTrimTarget::nits_100, 1.0, 0.0, 1.0, 1.0});
  HdrFrameMetadata dolby;
  dolby.standard = HdrStandard::dolby_vision;
  dolby.dolby_vision = dv;
  assert(hdr.validate(dolby, &diagnostic) == DIGITOR_RESULT_OK);
  assert(hdr.package_metadata(dolby, packet, &diagnostic) == DIGITOR_RESULT_BACKEND_UNAVAILABLE);

  HdrAdapterCallbacks callbacks;
  callbacks.package_dolby_vision = [](const DolbyVisionMetadata& metadata,
                                      HdrPacket& out,
                                      std::string& error) {
    out.standard = HdrStandard::dolby_vision;
    out.transport = HdrMetadataTransport::rpu;
    out.timestamp_us = metadata.timestamp_us;
    out.payload = {std::byte{0x19}, std::byte{0x08}};
    error.clear();
    return DIGITOR_RESULT_OK;
  };
  HdrEcosystem licensed(std::move(callbacks));
  assert(licensed.package_metadata(dolby, packet, &diagnostic) == DIGITOR_RESULT_OK);
  assert(packet.transport == HdrMetadataTransport::rpu);

  const auto telemetry = hdr.telemetry();
  assert(telemetry.analyzed_frames == 1u);
  assert(telemetry.tone_mapped_pixels == pixels.size());
  assert(telemetry.dynamic_metadata_frames == 1u);
  assert(telemetry.scene_changes == 1u);
  return 0;
}
