#include "digitor/audio_time_transform.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace digitor;

namespace {

std::vector<float> sine(std::uint32_t sample_rate, double frequency,
                        std::size_t frames, std::uint32_t channels) {
    std::vector<float> samples(frames * channels);
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float value = static_cast<float>(
            std::sin(2.0 * pi * frequency * static_cast<double>(frame) /
                     static_cast<double>(sample_rate)));
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            samples[frame * channels + channel] = value;
        }
    }
    return samples;
}

AudioTimeTransformConfig base_config() {
    AudioTimeTransformConfig config;
    config.source_sample_rate = 48000;
    config.destination_sample_rate = 48000;
    config.channels = 2;
    config.grain_frames = 512;
    config.overlap_frames = 256;
    return config;
}

void test_identity() {
    const auto input = sine(48000, 440.0, 4096, 2);
    const auto result = transform_audio_time(base_config(), input);
    assert(result.status == AudioTransformStatus::ok);
    assert(result.samples == input);
    assert(result.output_frames == 4096);
}

void test_sample_rate_conversion() {
    auto config = base_config();
    config.destination_sample_rate = 24000;
    const auto result = transform_audio_time(config, sine(48000, 440.0, 4800, 2));
    assert(result.status == AudioTransformStatus::ok);
    assert(result.output_frames == 2400);
    assert(result.samples.size() == 4800);
}

void test_non_preserving_rate_changes_duration() {
    auto config = base_config();
    config.playback_rate = 2.0;
    config.preserve_pitch = false;
    const auto result = transform_audio_time(config, sine(48000, 440.0, 4800, 2));
    assert(result.status == AudioTransformStatus::ok);
    assert(result.output_frames == 2400);
}

void test_pitch_preserving_stretch_changes_duration() {
    auto config = base_config();
    config.playback_rate = 0.5;
    config.preserve_pitch = true;
    const auto result = transform_audio_time(config, sine(48000, 440.0, 4096, 2));
    assert(result.status == AudioTransformStatus::ok);
    assert(result.output_frames == 8192);
    float peak = 0.0f;
    for (const float sample : result.samples) peak = std::max(peak, std::abs(sample));
    assert(peak > 0.5f && peak <= 1.1f);
}

void test_preview_export_identity() {
    auto config = base_config();
    config.playback_rate = 1.25;
    config.destination_sample_rate = 44100;
    const auto input = sine(48000, 997.0, 8192, 2);
    const auto preview = transform_audio_preview(config, input);
    const auto exported = transform_audio_export(config, input);
    assert(preview.status == AudioTransformStatus::ok);
    assert(preview.samples == exported.samples);
    assert(preview.output_frames == exported.output_frames);
}

void test_validation() {
    auto config = base_config();
    config.playback_rate = 0.0;
    std::string diagnostic;
    assert(validate_audio_time_transform(config, {0.0f, 0.0f}, diagnostic) ==
           AudioTransformStatus::invalid_configuration);

    config = base_config();
    config.overlap_frames = config.grain_frames;
    assert(validate_audio_time_transform(config, {0.0f, 0.0f}, diagnostic) ==
           AudioTransformStatus::invalid_configuration);

    config = base_config();
    assert(validate_audio_time_transform(config, {0.0f}, diagnostic) ==
           AudioTransformStatus::invalid_input);
}

} // namespace

int main() {
    test_identity();
    test_sample_rate_conversion();
    test_non_preserving_rate_changes_duration();
    test_pitch_preserving_stretch_changes_duration();
    test_preview_export_identity();
    test_validation();
    std::cout << "production audio time transform: PASS\n";
    return 0;
}
