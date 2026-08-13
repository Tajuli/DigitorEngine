#include "digitor/windows_zero_copy_concrete_bindings.hpp"
#include "digitor/windows_zero_copy_media_host.hpp"
#include "digitor/windows_zero_copy_production.hpp"

#include <cassert>
#include <string>

using namespace digitor;

int main() {
#if !defined(_WIN32)
  return 0;
#else
  // This is intentionally a link/availability contract, not a hardware
  // qualification run. Constructing the real Windows production components
  // forces the native engine artifact to carry their implementation objects.
  WindowsZeroCopyMediaHostOptions media_options{};
  media_options.media_path = "__digitor_link_contract_missing_media__.mp4";
  WindowsZeroCopyMediaHost media_host(nullptr, media_options);
  std::string diagnostic;
  const auto open_result = media_host.open(&diagnostic);
  assert(open_result != DIGITOR_RESULT_OK);
  assert(!diagnostic.empty());

  WindowsMediaFoundationEncoderConfig encoder_config{};
  WindowsMediaFoundationHardwareEncoder encoder(encoder_config, {});
  assert(encoder.initialize() != DIGITOR_RESULT_OK);
  assert(encoder.submitted_frames() == 0);

  WindowsZeroCopyProductionConfig production_config{};
  WindowsZeroCopyProductionPipeline production(std::move(production_config));
  assert(production.initialize() != DIGITOR_RESULT_OK);

  return 0;
#endif
}
