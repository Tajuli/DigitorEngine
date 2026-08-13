#include "digitor/windows_zero_copy_concrete_bindings.hpp"
#include "digitor/windows_zero_copy_production.hpp"
#include "digitor/windows_zero_copy_runtime.hpp"

#include <cassert>
#include <utility>

using namespace digitor;

int main() {
#if !defined(_WIN32)
  return 0;
#else
  // This is intentionally a production link/availability contract, not a
  // qualification run. Constructing the real Windows production components
  // forces the Flutter-consumed Engine artifact to carry the runtime, native
  // consumers, D3D12/P010 conversion and Media Foundation encoder objects.
  WindowsMediaFoundationEncoderConfig encoder_config{};
  WindowsMediaFoundationHardwareEncoder encoder(encoder_config, {});
  assert(encoder.initialize() != DIGITOR_RESULT_OK);
  assert(encoder.submitted_frames() == 0);

  WindowsZeroCopyRuntimeConfig runtime_config{};
  WindowsZeroCopyRuntime runtime(runtime_config, {}, {}, {});
  assert(runtime.initialize() != DIGITOR_RESULT_OK);

  WindowsZeroCopyProductionConfig production_config{};
  WindowsZeroCopyProductionPipeline production(std::move(production_config));
  assert(production.initialize() != DIGITOR_RESULT_OK);

  return 0;
#endif
}
