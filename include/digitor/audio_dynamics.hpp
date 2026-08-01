#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class AudioDynamicsStatus : std::uint8_t {
    ok = 0,
    invalid_configuration,
    invalid_input,
    processing_failed
};

enum class EqFilterType : std::uint8_t {
    low_shelf = 0,
    peak,
    high_shelf
};

struct ParametricEqBand {
    EqFilterType type{EqFilterType::peak};
    double frequency_hz{1000.0};
    double gain_db{};
    double q{0.7071067811865476};
    bool enabled{true};
};

struct CompressorConfig {
    bool enabled{true};
    double threshold_db{-18.0};
    double ratio{4.0};
    double attack_ms{10.0};
    double release_ms{100.0};
    double knee_db{6.0};
    double makeup_gain_db{};
};

struct TruePeakLimiterConfig {
    bool enabled{true};
    double ceiling_db{-1.0};
    double release_ms{80.0};
};

struct AudioDynamicsConfig {
    std::uint32_t sample_rate{48000};
    std::uint32_t channels{2};
    std::vector<ParametricEqBand> eq_bands;
    CompressorConfig compressor;
    TruePeakLimiterConfig limiter;
};

struct AudioLoudnessMetrics {
    double momentary_lufs{-120.0};
    double short_term_lufs{-120.0};
    double integrated_lufs{-120.0};
    double loudness_range_lu{};
    double true_peak_dbfs{-120.0};
};

struct AudioDynamicsTelemetry {
    double maximum_gain_reduction_db{};
    std::uint64_t limited_sample_count{};
};

struct AudioDynamicsResult {
    AudioDynamicsStatus status{AudioDynamicsStatus::ok};
    std::string diagnostic;
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    std::vector<float> samples;
    AudioLoudnessMetrics loudness;
    AudioDynamicsTelemetry telemetry;
};

[[nodiscard]] AudioDynamicsStatus validate_audio_dynamics(
    const AudioDynamicsConfig& config,
    const std::vector<float>& interleaved_samples,
    std::string& diagnostic) noexcept;

[[nodiscard]] AudioDynamicsResult process_audio_dynamics(
    const AudioDynamicsConfig& config,
    const std::vector<float>& interleaved_samples) noexcept;

[[nodiscard]] AudioDynamicsResult process_audio_dynamics_preview(
    const AudioDynamicsConfig& config,
    const std::vector<float>& interleaved_samples) noexcept;

[[nodiscard]] AudioDynamicsResult process_audio_dynamics_export(
    const AudioDynamicsConfig& config,
    const std::vector<float>& interleaved_samples) noexcept;

} // namespace digitor
