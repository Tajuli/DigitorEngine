#pragma once

#include <cstdint>
#include <vector>

namespace digitor {

enum class AudioMixStatus : std::uint32_t {
  invalid = 0,
  ready = 1,
};

struct AudioBuffer final {
  std::uint32_t sample_rate{};
  std::uint32_t channels{};
  std::vector<float> samples;
};

struct AudioTrackSettings final {
  float gain_db{};
  float pan{};
  float fade_in_seconds{};
  float fade_out_seconds{};
  bool mute{};
};

struct AudioMasterSettings final {
  float master_gain_db{};
  float limiter_ceiling_db{-1.0f};
  float limiter_release_ms{80.0f};
  bool limiter_enabled{true};
};

struct AudioMixMetrics final {
  float peak_dbfs{-120.0f};
  float rms_dbfs{-120.0f};
  float integrated_lufs{-120.0f};
  std::uint64_t digest{};
};

struct AudioMixResult final {
  AudioMixStatus status{AudioMixStatus::invalid};
  AudioMixMetrics metrics;
};

[[nodiscard]] std::uint64_t audio_buffer_digest(const AudioBuffer& buffer) noexcept;
[[nodiscard]] AudioMixResult mix_audio_reference(
    const std::vector<AudioBuffer>& tracks,
    const std::vector<AudioTrackSettings>& track_settings,
    AudioBuffer& output,
    const AudioMasterSettings& master_settings);

}  // namespace digitor

extern "C" {

struct DigitorAudioTrackSettings {
  float gain_db;
  float pan;
  float fade_in_seconds;
  float fade_out_seconds;
  std::uint32_t mute;
};

struct DigitorAudioMasterSettings {
  float master_gain_db;
  float limiter_ceiling_db;
  float limiter_release_ms;
  std::uint32_t limiter_enabled;
};

struct DigitorAudioMixMetrics {
  float peak_dbfs;
  float rms_dbfs;
  float integrated_lufs;
  std::uint64_t digest;
};

std::uint32_t digitor_mix_audio_f32(
    const float* const* track_samples,
    const std::uint64_t* track_frame_counts,
    const DigitorAudioTrackSettings* track_settings,
    std::uint32_t track_count,
    std::uint32_t sample_rate,
    std::uint32_t channels,
    float* output_samples,
    std::uint64_t output_frame_count,
    const DigitorAudioMasterSettings* master_settings,
    DigitorAudioMixMetrics* metrics);

}
