#include "digitor/audio_mixer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace digitor {
namespace {

constexpr double kPi = 3.14159265358979323846;

float db_to_linear(double db) noexcept {
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

float fade_gain(const AudioTrackMix& track,
                std::int64_t local_sample,
                std::int64_t total_samples) noexcept {
    double gain = 1.0;
    if (track.fade_in_samples > 0 && local_sample < track.fade_in_samples) {
        gain *= static_cast<double>(local_sample) /
                static_cast<double>(track.fade_in_samples);
    }
    if (track.fade_out_samples > 0) {
        const auto remaining = total_samples - local_sample;
        if (remaining <= track.fade_out_samples) {
            gain *= std::max(0.0, static_cast<double>(remaining) /
                                  static_cast<double>(track.fade_out_samples));
        }
    }
    return static_cast<float>(std::clamp(gain, 0.0, 1.0));
}

float soft_limit(float value, std::uint64_t& clipped) noexcept {
    if (std::abs(value) > 1.0f) ++clipped;
    return std::tanh(value);
}

} // namespace

AudioMixStatus validate_audio_mix(
    const AudioMixerConfig& config,
    const std::vector<AudioPcmBlock>& blocks,
    const std::vector<AudioTrackMix>& tracks,
    std::string& diagnostic) noexcept {
    try {
        if (config.sample_rate < 8000 || config.sample_rate > 384000 ||
            config.channels == 0 || config.channels > 8 ||
            config.output_start_sample < 0 || config.output_sample_count <= 0 ||
            !std::isfinite(config.master_gain_db)) {
            diagnostic = "audio mixer configuration is invalid";
            return AudioMixStatus::invalid_configuration;
        }
        std::unordered_set<std::uint64_t> track_ids;
        for (const auto& track : tracks) {
            if (track.clip_id == 0 || !track_ids.insert(track.clip_id).second ||
                !std::isfinite(track.gain_db) || !std::isfinite(track.pan) ||
                track.pan < -1.0 || track.pan > 1.0 ||
                track.fade_in_samples < 0 || track.fade_out_samples < 0) {
                diagnostic = "audio track mix controls are invalid";
                return AudioMixStatus::invalid_input;
            }
        }
        for (const auto& block : blocks) {
            if (block.clip_id == 0 || block.sample_rate != config.sample_rate ||
                block.channels == 0 || block.channels > config.channels ||
                block.destination_start_sample < 0 || block.samples.empty() ||
                block.samples.size() % block.channels != 0) {
                diagnostic = "audio PCM block is invalid or incompatible";
                return AudioMixStatus::invalid_input;
            }
        }
        diagnostic = "audio mix valid";
        return AudioMixStatus::ok;
    } catch (...) {
        diagnostic = "audio mix validation failed with an internal exception";
        return AudioMixStatus::invalid_input;
    }
}

AudioMixResult mix_audio_blocks(
    const AudioMixerConfig& config,
    const std::vector<AudioPcmBlock>& blocks,
    const std::vector<AudioTrackMix>& tracks) noexcept {
    AudioMixResult result;
    result.sample_rate = config.sample_rate;
    result.channels = config.channels;
    result.start_sample = config.output_start_sample;
    result.status = validate_audio_mix(config, blocks, tracks, result.diagnostic);
    if (result.status != AudioMixStatus::ok) return result;

    try {
        const auto output_frames = static_cast<std::size_t>(config.output_sample_count);
        result.samples.assign(output_frames * config.channels, 0.0f);
        std::unordered_map<std::uint64_t, AudioTrackMix> controls;
        bool any_solo = false;
        for (const auto& track : tracks) {
            controls.emplace(track.clip_id, track);
            any_solo = any_solo || track.solo;
        }

        for (const auto& block : blocks) {
            AudioTrackMix control;
            control.clip_id = block.clip_id;
            if (const auto found = controls.find(block.clip_id); found != controls.end()) {
                control = found->second;
            }
            if (control.muted || (any_solo && !control.solo)) continue;

            const auto source_frames = static_cast<std::int64_t>(
                block.samples.size() / block.channels);
            const auto block_start = block.destination_start_sample;
            const auto block_end = block_start + source_frames;
            const auto mix_start = std::max(block_start, config.output_start_sample);
            const auto mix_end = std::min(block_end,
                config.output_start_sample + config.output_sample_count);
            if (mix_end <= mix_start) continue;

            const float track_gain = db_to_linear(control.gain_db);
            const double pan_angle = (control.pan + 1.0) * kPi * 0.25;
            const float left_pan = static_cast<float>(std::cos(pan_angle));
            const float right_pan = static_cast<float>(std::sin(pan_angle));

            for (auto timeline_sample = mix_start;
                 timeline_sample < mix_end; ++timeline_sample) {
                const auto source_frame = timeline_sample - block_start;
                const auto output_frame = timeline_sample - config.output_start_sample;
                const float envelope = fade_gain(control, source_frame, source_frames);
                for (std::uint32_t channel = 0; channel < config.channels; ++channel) {
                    float sample = 0.0f;
                    if (block.channels == 1) {
                        sample = block.samples[static_cast<std::size_t>(source_frame)];
                    } else if (channel < block.channels) {
                        sample = block.samples[static_cast<std::size_t>(source_frame) *
                                               block.channels + channel];
                    }
                    float pan_gain = 1.0f;
                    if (config.channels >= 2) {
                        if (channel == 0) pan_gain = left_pan;
                        else if (channel == 1) pan_gain = right_pan;
                    }
                    const auto index = static_cast<std::size_t>(output_frame) *
                                       config.channels + channel;
                    result.samples[index] += sample * track_gain * envelope * pan_gain;
                }
            }
        }

        const float master_gain = db_to_linear(config.master_gain_db);
        result.meters.assign(config.channels, {});
        std::vector<long double> squares(config.channels, 0.0L);
        for (std::size_t frame = 0; frame < output_frames; ++frame) {
            for (std::uint32_t channel = 0; channel < config.channels; ++channel) {
                const auto index = frame * config.channels + channel;
                float sample = result.samples[index] * master_gain;
                sample = config.enable_soft_limiter
                    ? soft_limit(sample, result.clipped_sample_count)
                    : sample;
                if (!config.enable_soft_limiter && std::abs(sample) > 1.0f) {
                    ++result.clipped_sample_count;
                }
                result.samples[index] = sample;
                auto& meter = result.meters[channel];
                meter.peak = std::max(meter.peak, std::abs(sample));
                squares[channel] += static_cast<long double>(sample) * sample;
            }
        }
        for (std::uint32_t channel = 0; channel < config.channels; ++channel) {
            result.meters[channel].rms = static_cast<float>(
                std::sqrt(squares[channel] / static_cast<long double>(output_frames)));
        }
        result.status = AudioMixStatus::ok;
        result.diagnostic = "audio mix complete";
        return result;
    } catch (...) {
        result.status = AudioMixStatus::invalid_input;
        result.diagnostic = "audio mixing failed with an internal exception";
        result.samples.clear();
        result.meters.clear();
        return result;
    }
}

AudioMixResult mix_audio_preview(
    const AudioMixerConfig& config,
    const std::vector<AudioPcmBlock>& blocks,
    const std::vector<AudioTrackMix>& tracks) noexcept {
    return mix_audio_blocks(config, blocks, tracks);
}

AudioMixResult mix_audio_export(
    const AudioMixerConfig& config,
    const std::vector<AudioPcmBlock>& blocks,
    const std::vector<AudioTrackMix>& tracks) noexcept {
    return mix_audio_blocks(config, blocks, tracks);
}

} // namespace digitor
