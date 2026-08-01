#include "digitor/audio_time_transform.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace digitor {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> linear_resample(const std::vector<float>& input,
                                   std::uint32_t channels,
                                   double ratio) {
    const auto input_frames = input.size() / channels;
    if (input_frames == 0) return {};
    const auto output_frames = static_cast<std::size_t>(
        std::max(1.0, std::floor(static_cast<double>(input_frames) * ratio + 0.5)));
    std::vector<float> output(output_frames * channels, 0.0f);
    for (std::size_t frame = 0; frame < output_frames; ++frame) {
        const double source_position = static_cast<double>(frame) / ratio;
        const auto left = static_cast<std::size_t>(std::floor(source_position));
        const auto right = std::min(left + 1, input_frames - 1);
        const float fraction = static_cast<float>(source_position - std::floor(source_position));
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const float a = input[left * channels + channel];
            const float b = input[right * channels + channel];
            output[frame * channels + channel] = a + (b - a) * fraction;
        }
    }
    return output;
}

std::vector<float> granular_stretch(const std::vector<float>& input,
                                    std::uint32_t channels,
                                    double playback_rate,
                                    std::uint32_t grain_frames,
                                    std::uint32_t overlap_frames) {
    const auto input_frames = input.size() / channels;
    if (input_frames == 0) return {};
    if (input_frames <= grain_frames) {
        return linear_resample(input, channels, 1.0 / playback_rate);
    }

    const auto output_frames = static_cast<std::size_t>(
        std::max(1.0, std::floor(static_cast<double>(input_frames) / playback_rate + 0.5)));
    const std::uint32_t output_hop = grain_frames - overlap_frames;
    const double input_hop = static_cast<double>(output_hop) * playback_rate;
    std::vector<float> output((output_frames + grain_frames) * channels, 0.0f);
    std::vector<float> weights(output_frames + grain_frames, 0.0f);

    std::size_t output_start = 0;
    double input_start = 0.0;
    while (output_start < output_frames) {
        const auto source_start = static_cast<std::size_t>(std::floor(input_start));
        if (source_start >= input_frames) break;
        const auto available = std::min<std::size_t>(grain_frames, input_frames - source_start);
        for (std::size_t local = 0; local < available; ++local) {
            const double phase = grain_frames > 1
                ? static_cast<double>(local) / static_cast<double>(grain_frames - 1)
                : 0.0;
            const float window = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * phase));
            const auto destination = output_start + local;
            if (destination >= weights.size()) break;
            weights[destination] += window;
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                output[destination * channels + channel] +=
                    input[(source_start + local) * channels + channel] * window;
            }
        }
        output_start += output_hop;
        input_start += input_hop;
    }

    output.resize(output_frames * channels);
    weights.resize(output_frames);
    for (std::size_t frame = 0; frame < output_frames; ++frame) {
        const float weight = weights[frame];
        if (weight <= std::numeric_limits<float>::epsilon()) continue;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            output[frame * channels + channel] /= weight;
        }
    }
    return output;
}

} // namespace

AudioTransformStatus validate_audio_time_transform(
    const AudioTimeTransformConfig& config,
    const std::vector<float>& interleaved_samples,
    std::string& diagnostic) noexcept {
    try {
        if (config.source_sample_rate < 8000 || config.source_sample_rate > 384000 ||
            config.destination_sample_rate < 8000 || config.destination_sample_rate > 384000 ||
            config.channels == 0 || config.channels > 8 ||
            !std::isfinite(config.playback_rate) || config.playback_rate < 0.25 ||
            config.playback_rate > 4.0) {
            diagnostic = "audio time-transform configuration is invalid";
            return AudioTransformStatus::invalid_configuration;
        }
        if (config.preserve_pitch &&
            (config.grain_frames < 64 || config.grain_frames > 8192 ||
             config.overlap_frames == 0 || config.overlap_frames >= config.grain_frames)) {
            diagnostic = "pitch-preserving grain configuration is invalid";
            return AudioTransformStatus::invalid_configuration;
        }
        if (interleaved_samples.empty() || interleaved_samples.size() % config.channels != 0) {
            diagnostic = "interleaved PCM input is invalid";
            return AudioTransformStatus::invalid_input;
        }
        for (const float sample : interleaved_samples) {
            if (!std::isfinite(sample)) {
                diagnostic = "interleaved PCM contains a non-finite sample";
                return AudioTransformStatus::invalid_input;
            }
        }
        diagnostic = "audio time-transform valid";
        return AudioTransformStatus::ok;
    } catch (...) {
        diagnostic = "audio time-transform validation failed with an internal exception";
        return AudioTransformStatus::invalid_input;
    }
}

AudioTimeTransformResult transform_audio_time(
    const AudioTimeTransformConfig& config,
    const std::vector<float>& interleaved_samples) noexcept {
    AudioTimeTransformResult result;
    result.sample_rate = config.destination_sample_rate;
    result.channels = config.channels;
    result.source_frames = interleaved_samples.size() / config.channels;
    result.status = validate_audio_time_transform(config, interleaved_samples, result.diagnostic);
    if (result.status != AudioTransformStatus::ok) return result;

    try {
        std::vector<float> rate_adjusted;
        if (config.preserve_pitch && std::abs(config.playback_rate - 1.0) > 1e-12) {
            rate_adjusted = granular_stretch(interleaved_samples, config.channels,
                                             config.playback_rate, config.grain_frames,
                                             config.overlap_frames);
        } else if (std::abs(config.playback_rate - 1.0) > 1e-12) {
            rate_adjusted = linear_resample(interleaved_samples, config.channels,
                                            1.0 / config.playback_rate);
        } else {
            rate_adjusted = interleaved_samples;
        }

        const double sample_rate_ratio =
            static_cast<double>(config.destination_sample_rate) /
            static_cast<double>(config.source_sample_rate);
        result.samples = std::abs(sample_rate_ratio - 1.0) > 1e-12
            ? linear_resample(rate_adjusted, config.channels, sample_rate_ratio)
            : std::move(rate_adjusted);
        result.output_frames = result.samples.size() / config.channels;
        result.status = AudioTransformStatus::ok;
        result.diagnostic = "audio time-transform complete";
        return result;
    } catch (...) {
        result.status = AudioTransformStatus::processing_failed;
        result.diagnostic = "audio time-transform failed with an internal exception";
        result.samples.clear();
        result.output_frames = 0;
        return result;
    }
}

AudioTimeTransformResult transform_audio_preview(
    const AudioTimeTransformConfig& config,
    const std::vector<float>& interleaved_samples) noexcept {
    return transform_audio_time(config, interleaved_samples);
}

AudioTimeTransformResult transform_audio_export(
    const AudioTimeTransformConfig& config,
    const std::vector<float>& interleaved_samples) noexcept {
    return transform_audio_time(config, interleaved_samples);
}

} // namespace digitor
