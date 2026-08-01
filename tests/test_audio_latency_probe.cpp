#include "digitor/audio_latency_probe.hpp"

#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}
}

int main() {
  using digitor::AudioLatencyBackend;
  const auto result = digitor::probe_default_audio_output();
  if (std::string(digitor::audio_latency_backend_name(result.backend)).empty())
    return fail("backend name must not be empty");
  if (result.diagnostic.empty()) return fail("probe diagnostic must not be empty");

  if (!result.available) {
#if defined(_WIN32) || defined(__APPLE__) || defined(__ANDROID__)
    // Headless CI machines may have no output device; unavailability must remain explicit.
    if (result.backend == AudioLatencyBackend::unavailable)
      return fail("supported platform must report its native backend even without a device");
#else
    if (result.backend != AudioLatencyBackend::unavailable)
      return fail("unsupported platform must report unavailable backend");
#endif
    return 0;
  }

  if (result.backend == AudioLatencyBackend::unavailable)
    return fail("available result cannot use unavailable backend");
  if (result.sample_rate < 8000 || result.sample_rate > 768000)
    return fail("sample rate outside supported bounds");
  if (result.buffer_frames == 0) return fail("available result requires buffer frames");
  if (result.device_latency_us < 0 || result.buffer_latency_us <= 0 || result.total_latency_us <= 0)
    return fail("latency values must be non-negative and total must be positive");
  if (result.total_latency_us != result.device_latency_us + result.buffer_latency_us)
    return fail("total latency must equal device plus buffer latency");
  return 0;
}
