#include "digitor/audio_dynamics.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace digitor;

namespace {

std::vector<float> sine(std::uint32_t sample_rate, double frequency,
                        std::size_t frames, std::uint32_t channels,
                        float amplitude = 0.5f) {
    std::vector<float> samples(frames * channels);
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float value = amplitude * static_cast<float>(
            std::sin(2.0 * pi * frequency * static_cast<double>(frame) /
                     static_cast<double>(sample_rate)));
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            samples[frame * channels + channel] = value;
        }
    }
    return samples;
}

AudioDynamicsConfig base_config() {
    AudioDynamicsConfig config;
    config.sample_rate = 48000;
    config.channels = 2;
    config.compressor.enabled = false;
    config.limiter.enabled = false;
    return config;
}

float peak(const std::vector<float>& samples) {
    float value = 0.0f;
    for (float sample : samples) value = std::max(value, std::abs(sample));
    return value;
}

void test_identity() {
    const auto input = sine(48000, 440.0, 4096, 2);
    const auto result = process_audio_dynamics(base_config(), input);
    assert(result.status == AudioDynamicsStatus::ok);
    assert(result.samples == input);
}

void test_parametric_eq_changes_signal() {
    auto config = base_config();
    ParametricEqBand band;
    band.type = EqFilterType::peak;
    band.frequency_hz = 1000.0;
    band.gain_db = 12.0;
    band.q = 1.0;
    config.eq_bands.push_back(band);
    const auto input = sine(48000, 1000.0, 48000, 2, 0.1f);
    const auto result = process_audio_dynamics(config, input);
    assert(result.status == AudioDynamicsStatus::ok);
    assert(peak(result.samples) > peak(input) * 2.0f);
}

void test_compressor_reduces_peak() {
    auto config = base_config();
    config.compressor.enabled = true;
    config.compressor.threshold_db = -18.0;
    config.compressor.ratio = 8.0;
    config.compressor.attack_ms = 1.0;
    config.compressor.release_ms = 50.0;
    const auto input = sine(48000, 440.0, 48000, 2, 0.95f);
    const auto result = process_audio_dynamics(config, input);
    assert(result.status == AudioDynamicsStatus::ok);
    assert(result.telemetry.maximum_gain_reduction_db > 6.0);
    assert(peak(result.samples) < peak(input));
}

void test_limiter_respects_ceiling() {
    auto config = base_config();
    config.limiter.enabled = true;
    config.limiter.ceiling_db = -1.0;
    const auto input = sine(48000, 440.0, 48000, 2, 2.0f);
    const auto result = process_audio_dynamics(config, input);
    assert(result.status == AudioDynamicsStatus::ok);
    assert(result.telemetry.limited_sample_count > 0);
    assert(peak(result.samples) <= 0.892f);
}

void test_loudness_metrics() {
    auto config = base_config();
    const auto result = process_audio_dynamics(config,
        sine(48000, 997.0, 48000 * 4, 2, 0.2f));
    assert(result.status == AudioDynamicsStatus::ok);
    assert(result.loudness.integrated_lufs > -30.0);
    assert(result.loudness.integrated_lufs < -10.0);
    assert(result.loudness.momentary_lufs > -120.0);
    assert(result.loudness.short_term_lufs > -120.0);
    assert(result.loudness.true_peak_dbfs < 0.0);
}

void test_preview_export_identity() {
    auto config = base_config();
    config.compressor.enabled = true;
    config.limiter.enabled = true;
    config.eq_bands.push_back({EqFilterType::low_shelf, 120.0, 3.0, 0.707, true});
    const auto input = sine(48000, 220.0, 8192, 2, 0.7f);
    const auto preview = process_audio_dynamics_preview(config, input);
    const auto exported = process_audio_dynamics_export(config, input);
    assert(preview.status == AudioDynamicsStatus::ok);
    assert(preview.samples == exported.samples);
    assert(preview.loudness.integrated_lufs == exported.loudness.integrated_lufs);
    assert(preview.telemetry.limited_sample_count == exported.telemetry.limited_sample_count);
}

void test_validation() {
    std::string diagnostic;
    auto config = base_config();
    config.channels = 0;
    assert(validate_audio_dynamics(config, {0.0f, 0.0f}, diagnostic) ==
           AudioDynamicsStatus::invalid_configuration);

    config = base_config();
    config.eq_bands.push_back({EqFilterType::peak, 30000.0, 0.0, 1.0, true});
    assert(validate_audio_dynamics(config, {0.0f, 0.0f}, diagnostic) ==
           AudioDynamicsStatus::invalid_configuration);

    config = base_config();
    assert(validate_audio_dynamics(config,
        {0.0f, std::numeric_limits<float>::infinity()}, diagnostic) ==
        AudioDynamicsStatus::invalid_input);
}

} // namespace

int main() {
    test_identity();
    test_parametric_eq_changes_signal();
    test_compressor_reduces_peak();
    test_limiter_respects_ceiling();
    test_loudness_metrics();
    test_preview_export_identity();
    test_validation();
    std::cout << "production audio dynamics and loudness: PASS\n";
    return 0;
}
