#pragma once

#include <cstdint>
#include <string>

namespace digitor {

enum class AudioLatencyBackend {
  unavailable,
  wasapi,
  aaudio,
  core_audio,
};

struct AudioLatencyProbeResult {
  AudioLatencyBackend backend{AudioLatencyBackend::unavailable};
  bool available{};
  std::uint32_t sample_rate{};
  std::uint32_t buffer_frames{};
  std::uint32_t safety_frames{};
  std::int64_t device_latency_us{};
  std::int64_t buffer_latency_us{};
  std::int64_t total_latency_us{};
  std::string diagnostic;
};

[[nodiscard]] AudioLatencyProbeResult probe_default_audio_output() noexcept;
[[nodiscard]] const char* audio_latency_backend_name(AudioLatencyBackend backend) noexcept;

}  // namespace digitor
