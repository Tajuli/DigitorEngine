#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class AudioTransformStatus : std::uint8_t {
    ok = 0,
    invalid_configuration,
    invalid_input,
    processing_failed
};

struct AudioTimeTransformConfig {
    std::uint32_t source_sample_rate{48000};
    std::uint32_t destination_sample_rate{48000};
    std::uint32_t channels{2};
    double playback_rate{1.0};
    bool preserve_pitch{true};
    std::uint32_t grain_frames{1024};
    std::uint32_t overlap_frames{512};
};

struct AudioTimeTransformResult {
    AudioTransformStatus status{AudioTransformStatus::ok};
    std::string diagnostic;
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    std::vector<float> samples;
    std::uint64_t source_frames{};
    std::uint64_t output_frames{};
};

[[nodiscard]] AudioTransformStatus validate_audio_time_transform(
    const AudioTimeTransformConfig& config,
    const std::vector<float>& interleaved_samples,
    std::string& diagnostic) noexcept;

[[nodiscard]] AudioTimeTransformResult transform_audio_time(
    const AudioTimeTransformConfig& config,
    const std::vector<float>& interleaved_samples) noexcept;

[[nodiscard]] AudioTimeTransformResult transform_audio_preview(
    const AudioTimeTransformConfig& config,
    const std::vector<float>& interleaved_samples) noexcept;

[[nodiscard]] AudioTimeTransformResult transform_audio_export(
    const AudioTimeTransformConfig& config,
    const std::vector<float>& interleaved_samples) noexcept;

} // namespace digitor
