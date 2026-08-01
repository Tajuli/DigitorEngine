#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class AudioMixStatus : std::uint8_t {
    ok = 0,
    invalid_configuration,
    invalid_input
};

struct AudioPcmBlock {
    std::uint64_t clip_id{};
    std::uint32_t sample_rate{48000};
    std::uint32_t channels{2};
    std::int64_t destination_start_sample{};
    std::vector<float> samples;
};

struct AudioTrackMix {
    std::uint64_t clip_id{};
    double gain_db{};
    double pan{};
    bool muted{};
    bool solo{};
    std::int64_t fade_in_samples{};
    std::int64_t fade_out_samples{};
};

struct AudioMixerConfig {
    std::uint32_t sample_rate{48000};
    std::uint32_t channels{2};
    std::int64_t output_start_sample{};
    std::int64_t output_sample_count{};
    double master_gain_db{};
    bool enable_soft_limiter{true};
};

struct AudioChannelMeter {
    float peak{};
    float rms{};
};

struct AudioMixResult {
    AudioMixStatus status{AudioMixStatus::ok};
    std::string diagnostic;
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    std::int64_t start_sample{};
    std::vector<float> samples;
    std::vector<AudioChannelMeter> meters;
    std::uint64_t clipped_sample_count{};
};

[[nodiscard]] AudioMixStatus validate_audio_mix(
    const AudioMixerConfig& config,
    const std::vector<AudioPcmBlock>& blocks,
    const std::vector<AudioTrackMix>& tracks,
    std::string& diagnostic) noexcept;

[[nodiscard]] AudioMixResult mix_audio_blocks(
    const AudioMixerConfig& config,
    const std::vector<AudioPcmBlock>& blocks,
    const std::vector<AudioTrackMix>& tracks) noexcept;

[[nodiscard]] AudioMixResult mix_audio_preview(
    const AudioMixerConfig& config,
    const std::vector<AudioPcmBlock>& blocks,
    const std::vector<AudioTrackMix>& tracks) noexcept;

[[nodiscard]] AudioMixResult mix_audio_export(
    const AudioMixerConfig& config,
    const std::vector<AudioPcmBlock>& blocks,
    const std::vector<AudioTrackMix>& tracks) noexcept;

} // namespace digitor
