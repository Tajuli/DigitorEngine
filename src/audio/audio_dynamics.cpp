#include "digitor/audio_dynamics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace digitor {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSilenceLufs = -120.0;

float db_to_linear(double db) noexcept {
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

double linear_to_db(double value) noexcept {
    return value <= 1e-12 ? -120.0 : 20.0 * std::log10(value);
}

struct Biquad {
    double b0{1.0};
    double b1{};
    double b2{};
    double a1{};
    double a2{};
    double z1{};
    double z2{};

    float process(float input) noexcept {
        const double output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return static_cast<float>(output);
    }
};

Biquad make_filter(const ParametricEqBand& band, std::uint32_t sample_rate) {
    const double amplitude = std::pow(10.0, band.gain_db / 40.0);
    const double omega = 2.0 * kPi * band.frequency_hz / sample_rate;
    const double cosine = std::cos(omega);
    const double sine = std::sin(omega);
    const double alpha = sine / (2.0 * band.q);
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

    if (band.type == EqFilterType::peak) {
        b0 = 1.0 + alpha * amplitude;
        b1 = -2.0 * cosine;
        b2 = 1.0 - alpha * amplitude;
        a0 = 1.0 + alpha / amplitude;
        a1 = -2.0 * cosine;
        a2 = 1.0 - alpha / amplitude;
    } else {
        const double root_a = std::sqrt(amplitude);
        const double shelf_alpha = sine * std::sqrt(2.0) * 0.5;
        if (band.type == EqFilterType::low_shelf) {
            b0 = amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine + 2.0 * root_a * shelf_alpha);
            b1 = 2.0 * amplitude * ((amplitude - 1.0) - (amplitude + 1.0) * cosine);
            b2 = amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine - 2.0 * root_a * shelf_alpha);
            a0 = (amplitude + 1.0) + (amplitude - 1.0) * cosine + 2.0 * root_a * shelf_alpha;
            a1 = -2.0 * ((amplitude - 1.0) + (amplitude + 1.0) * cosine);
            a2 = (amplitude + 1.0) + (amplitude - 1.0) * cosine - 2.0 * root_a * shelf_alpha;
        } else {
            b0 = amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine + 2.0 * root_a * shelf_alpha);
            b1 = -2.0 * amplitude * ((amplitude - 1.0) + (amplitude + 1.0) * cosine);
            b2 = amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine - 2.0 * root_a * shelf_alpha);
            a0 = (amplitude + 1.0) - (amplitude - 1.0) * cosine + 2.0 * root_a * shelf_alpha;
            a1 = 2.0 * ((amplitude - 1.0) - (amplitude + 1.0) * cosine);
            a2 = (amplitude + 1.0) - (amplitude - 1.0) * cosine - 2.0 * root_a * shelf_alpha;
        }
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0, 0.0, 0.0};
}

double compressor_reduction_db(double level_db, const CompressorConfig& config) noexcept {
    const double lower = config.threshold_db - config.knee_db * 0.5;
    const double upper = config.threshold_db + config.knee_db * 0.5;
    if (config.knee_db > 0.0 && level_db > lower && level_db < upper) {
        const double x = level_db - lower;
        const double compressed = (1.0 / config.ratio - 1.0) * x * x / (2.0 * config.knee_db);
        return -compressed;
    }
    if (level_db <= config.threshold_db) return 0.0;
    const double output = config.threshold_db + (level_db - config.threshold_db) / config.ratio;
    return level_db - output;
}

AudioLoudnessMetrics measure_loudness(const std::vector<float>& samples,
                                      std::uint32_t sample_rate,
                                      std::uint32_t channels) {
    AudioLoudnessMetrics metrics;
    const std::size_t frames = samples.size() / channels;
    if (frames == 0) return metrics;
    long double total_square = 0.0L;
    double peak = 0.0;
    std::vector<double> block_lufs;
    const std::size_t block_frames = std::max<std::size_t>(1, sample_rate * 400 / 1000);
    for (std::size_t block = 0; block < frames; block += block_frames) {
        const std::size_t end = std::min(frames, block + block_frames);
        long double square = 0.0L;
        std::size_t count = 0;
        for (std::size_t frame = block; frame < end; ++frame) {
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                const double value = samples[frame * channels + channel];
                square += value * value;
                total_square += value * value;
                peak = std::max(peak, std::abs(value));
                ++count;
            }
        }
        if (count > 0) {
            const double mean = static_cast<double>(square / count);
            block_lufs.push_back(mean <= 1e-12 ? kSilenceLufs : -0.691 + 10.0 * std::log10(mean));
        }
    }
    const double mean_square = static_cast<double>(total_square / samples.size());
    metrics.integrated_lufs = mean_square <= 1e-12 ? kSilenceLufs : -0.691 + 10.0 * std::log10(mean_square);
    metrics.momentary_lufs = block_lufs.empty() ? kSilenceLufs : block_lufs.back();
    const std::size_t short_blocks = std::max<std::size_t>(1, 3000 / 400);
    const auto begin = block_lufs.size() > short_blocks ? block_lufs.end() - static_cast<std::ptrdiff_t>(short_blocks) : block_lufs.begin();
    double linear_sum = 0.0;
    std::size_t linear_count = 0;
    for (auto it = begin; it != block_lufs.end(); ++it) {
        if (*it > kSilenceLufs) {
            linear_sum += std::pow(10.0, (*it + 0.691) / 10.0);
            ++linear_count;
        }
    }
    metrics.short_term_lufs = linear_count == 0 ? kSilenceLufs : -0.691 + 10.0 * std::log10(linear_sum / linear_count);
    std::sort(block_lufs.begin(), block_lufs.end());
    if (block_lufs.size() >= 2) {
        const auto low = block_lufs[block_lufs.size() / 10];
        const auto high = block_lufs[(block_lufs.size() * 95) / 100];
        metrics.loudness_range_lu = std::max(0.0, high - low);
    }
    metrics.true_peak_dbfs = linear_to_db(peak);
    return metrics;
}

} // namespace

AudioDynamicsStatus validate_audio_dynamics(const AudioDynamicsConfig& config,
                                            const std::vector<float>& samples,
                                            std::string& diagnostic) noexcept {
    try {
        if (config.sample_rate < 8000 || config.sample_rate > 384000 ||
            config.channels == 0 || config.channels > 8 || samples.empty() ||
            samples.size() % config.channels != 0) {
            diagnostic = "audio dynamics configuration or PCM layout is invalid";
            return AudioDynamicsStatus::invalid_configuration;
        }
        for (const auto& band : config.eq_bands) {
            if (!std::isfinite(band.frequency_hz) || !std::isfinite(band.gain_db) ||
                !std::isfinite(band.q) || band.frequency_hz < 20.0 ||
                band.frequency_hz >= config.sample_rate * 0.5 || band.q < 0.1 || band.q > 20.0 ||
                band.gain_db < -24.0 || band.gain_db > 24.0) {
                diagnostic = "parametric EQ band is invalid";
                return AudioDynamicsStatus::invalid_configuration;
            }
        }
        const auto& compressor = config.compressor;
        if (!std::isfinite(compressor.threshold_db) || !std::isfinite(compressor.ratio) ||
            !std::isfinite(compressor.attack_ms) || !std::isfinite(compressor.release_ms) ||
            !std::isfinite(compressor.knee_db) || !std::isfinite(compressor.makeup_gain_db) ||
            compressor.ratio < 1.0 || compressor.ratio > 40.0 || compressor.attack_ms <= 0.0 ||
            compressor.release_ms <= 0.0 || compressor.knee_db < 0.0 || compressor.knee_db > 24.0) {
            diagnostic = "compressor configuration is invalid";
            return AudioDynamicsStatus::invalid_configuration;
        }
        if (!std::isfinite(config.limiter.ceiling_db) || !std::isfinite(config.limiter.release_ms) ||
            config.limiter.ceiling_db > 0.0 || config.limiter.ceiling_db < -24.0 ||
            config.limiter.release_ms <= 0.0) {
            diagnostic = "limiter configuration is invalid";
            return AudioDynamicsStatus::invalid_configuration;
        }
        for (float sample : samples) {
            if (!std::isfinite(sample)) {
                diagnostic = "PCM contains a non-finite sample";
                return AudioDynamicsStatus::invalid_input;
            }
        }
        diagnostic = "audio dynamics valid";
        return AudioDynamicsStatus::ok;
    } catch (...) {
        diagnostic = "audio dynamics validation failed with an internal exception";
        return AudioDynamicsStatus::invalid_input;
    }
}

AudioDynamicsResult process_audio_dynamics(const AudioDynamicsConfig& config,
                                           const std::vector<float>& input) noexcept {
    AudioDynamicsResult result;
    result.sample_rate = config.sample_rate;
    result.channels = config.channels;
    result.status = validate_audio_dynamics(config, input, result.diagnostic);
    if (result.status != AudioDynamicsStatus::ok) return result;
    try {
        result.samples = input;
        std::vector<std::vector<Biquad>> filters(config.channels);
        for (std::uint32_t channel = 0; channel < config.channels; ++channel) {
            for (const auto& band : config.eq_bands) if (band.enabled) filters[channel].push_back(make_filter(band, config.sample_rate));
        }
        for (std::size_t frame = 0; frame < result.samples.size() / config.channels; ++frame) {
            for (std::uint32_t channel = 0; channel < config.channels; ++channel) {
                float value = result.samples[frame * config.channels + channel];
                for (auto& filter : filters[channel]) value = filter.process(value);
                result.samples[frame * config.channels + channel] = value;
            }
        }

        if (config.compressor.enabled) {
            double envelope = 0.0;
            const double attack = std::exp(-1.0 / (config.compressor.attack_ms * 0.001 * config.sample_rate));
            const double release = std::exp(-1.0 / (config.compressor.release_ms * 0.001 * config.sample_rate));
            const float makeup = db_to_linear(config.compressor.makeup_gain_db);
            for (std::size_t frame = 0; frame < result.samples.size() / config.channels; ++frame) {
                double detector = 0.0;
                for (std::uint32_t channel = 0; channel < config.channels; ++channel) detector = std::max(detector, std::abs(static_cast<double>(result.samples[frame * config.channels + channel])));
                envelope = detector > envelope ? attack * envelope + (1.0 - attack) * detector : release * envelope + (1.0 - release) * detector;
                const double reduction = compressor_reduction_db(linear_to_db(envelope), config.compressor);
                result.telemetry.maximum_gain_reduction_db = std::max(result.telemetry.maximum_gain_reduction_db, reduction);
                const float gain = db_to_linear(-reduction) * makeup;
                for (std::uint32_t channel = 0; channel < config.channels; ++channel) result.samples[frame * config.channels + channel] *= gain;
            }
        }

        if (config.limiter.enabled) {
            const float ceiling = db_to_linear(config.limiter.ceiling_db);
            double gain = 1.0;
            const double release = std::exp(-1.0 / (config.limiter.release_ms * 0.001 * config.sample_rate));
            for (std::size_t frame = 0; frame < result.samples.size() / config.channels; ++frame) {
                double peak = 0.0;
                for (std::uint32_t channel = 0; channel < config.channels; ++channel) peak = std::max(peak, std::abs(static_cast<double>(result.samples[frame * config.channels + channel])));
                const double target = peak > ceiling ? ceiling / peak : 1.0;
                if (target < gain) gain = target; else gain = release * gain + (1.0 - release);
                if (gain < 0.999999) ++result.telemetry.limited_sample_count;
                for (std::uint32_t channel = 0; channel < config.channels; ++channel) result.samples[frame * config.channels + channel] = static_cast<float>(result.samples[frame * config.channels + channel] * gain);
            }
        }
        result.loudness = measure_loudness(result.samples, config.sample_rate, config.channels);
        result.status = AudioDynamicsStatus::ok;
        result.diagnostic = "audio dynamics processing complete";
        return result;
    } catch (...) {
        result.status = AudioDynamicsStatus::processing_failed;
        result.diagnostic = "audio dynamics processing failed with an internal exception";
        result.samples.clear();
        return result;
    }
}

AudioDynamicsResult process_audio_dynamics_preview(const AudioDynamicsConfig& config,
                                                   const std::vector<float>& samples) noexcept {
    return process_audio_dynamics(config, samples);
}

AudioDynamicsResult process_audio_dynamics_export(const AudioDynamicsConfig& config,
                                                  const std::vector<float>& samples) noexcept {
    return process_audio_dynamics(config, samples);
}

} // namespace digitor
