#include "digitor/production_audio_mixing.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace digitor {
namespace {

constexpr float kSilenceDb = -120.0f;
constexpr float kPiOverFour = 0.7853981633974483f;

[[nodiscard]] bool finite(float value) noexcept {
  return std::isfinite(value);
}

[[nodiscard]] float db_to_linear(float db) noexcept {
  return std::pow(10.0f, db / 20.0f);
}

[[nodiscard]] float linear_to_db(float value) noexcept {
  return value > 1.0e-6f ? 20.0f * std::log10(value) : kSilenceDb;
}

[[nodiscard]] bool valid_buffer(const AudioBuffer& buffer) noexcept {
  return buffer.sample_rate > 0u &&
         (buffer.channels == 1u || buffer.channels == 2u) &&
         !buffer.samples.empty() &&
         buffer.samples.size() % buffer.channels == 0u;
}

[[nodiscard]] bool valid_track_settings(const AudioTrackSettings& settings) noexcept {
  return finite(settings.gain_db) && settings.gain_db >= -120.0f &&
         settings.gain_db <= 24.0f && finite(settings.pan) &&
         settings.pan >= -1.0f && settings.pan <= 1.0f &&
         finite(settings.fade_in_seconds) && settings.fade_in_seconds >= 0.0f &&
         finite(settings.fade_out_seconds) && settings.fade_out_seconds >= 0.0f;
}

[[nodiscard]] bool valid_master_settings(const AudioMasterSettings& settings) noexcept {
  return finite(settings.master_gain_db) && settings.master_gain_db >= -120.0f &&
         settings.master_gain_db <= 24.0f && finite(settings.limiter_ceiling_db) &&
         settings.limiter_ceiling_db >= -24.0f &&
         settings.limiter_ceiling_db <= 0.0f &&
         finite(settings.limiter_release_ms) &&
         settings.limiter_release_ms >= 1.0f &&
         settings.limiter_release_ms <= 5000.0f;
}

[[nodiscard]] float fade_gain(std::uint64_t frame,
                              std::uint64_t total_frames,
                              std::uint64_t fade_in_frames,
                              std::uint64_t fade_out_frames) noexcept {
  float gain = 1.0f;
  if (fade_in_frames > 0u && frame < fade_in_frames) {
    gain = std::min(gain, static_cast<float>(frame) /
                             static_cast<float>(fade_in_frames));
  }
  if (fade_out_frames > 0u && frame + fade_out_frames >= total_frames) {
    const std::uint64_t remaining = total_frames - frame - 1u;
    gain = std::min(gain, static_cast<float>(remaining) /
                             static_cast<float>(fade_out_frames));
  }
  return std::clamp(gain, 0.0f, 1.0f);
}

std::uint64_t append_digest(std::uint64_t hash,
                            const void* data,
                            std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash;
}

}  // namespace

std::uint64_t audio_buffer_digest(const AudioBuffer& buffer) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append_digest(hash, &buffer.sample_rate, sizeof(buffer.sample_rate));
  hash = append_digest(hash, &buffer.channels, sizeof(buffer.channels));
  if (!buffer.samples.empty()) {
    hash = append_digest(hash, buffer.samples.data(),
                         buffer.samples.size() * sizeof(float));
  }
  return hash;
}

AudioMixResult mix_audio_reference(
    const std::vector<AudioBuffer>& tracks,
    const std::vector<AudioTrackSettings>& track_settings,
    AudioBuffer& output,
    const AudioMasterSettings& master_settings) {
  AudioMixResult result;
  if (tracks.empty() || tracks.size() != track_settings.size() ||
      !valid_master_settings(master_settings)) {
    return result;
  }

  const std::uint32_t sample_rate = tracks.front().sample_rate;
  const std::uint32_t channels = tracks.front().channels;
  std::uint64_t frame_count = 0u;
  for (std::size_t index = 0; index < tracks.size(); ++index) {
    if (!valid_buffer(tracks[index]) ||
        tracks[index].sample_rate != sample_rate ||
        tracks[index].channels != channels ||
        !valid_track_settings(track_settings[index])) {
      return result;
    }
    frame_count = std::max(
        frame_count,
        static_cast<std::uint64_t>(tracks[index].samples.size() / channels));
  }

  output.sample_rate = sample_rate;
  output.channels = channels;
  output.samples.assign(static_cast<std::size_t>(frame_count) * channels, 0.0f);

  for (std::size_t track_index = 0; track_index < tracks.size(); ++track_index) {
    const auto& track = tracks[track_index];
    const auto& settings = track_settings[track_index];
    if (settings.mute) {
      continue;
    }
    const std::uint64_t track_frames = track.samples.size() / channels;
    const std::uint64_t fade_in_frames = static_cast<std::uint64_t>(
        settings.fade_in_seconds * static_cast<float>(sample_rate));
    const std::uint64_t fade_out_frames = static_cast<std::uint64_t>(
        settings.fade_out_seconds * static_cast<float>(sample_rate));
    const float base_gain = db_to_linear(settings.gain_db);
    const float angle = (settings.pan + 1.0f) * kPiOverFour;
    const float left_pan = channels == 2u ? std::cos(angle) : 1.0f;
    const float right_pan = channels == 2u ? std::sin(angle) : 1.0f;

    for (std::uint64_t frame = 0; frame < track_frames; ++frame) {
      const float gain = base_gain *
                         fade_gain(frame, track_frames, fade_in_frames,
                                   fade_out_frames);
      const std::size_t offset = static_cast<std::size_t>(frame) * channels;
      if (channels == 1u) {
        output.samples[offset] += track.samples[offset] * gain;
      } else {
        output.samples[offset] += track.samples[offset] * gain * left_pan;
        output.samples[offset + 1u] +=
            track.samples[offset + 1u] * gain * right_pan;
      }
    }
  }

  const float master_gain = db_to_linear(master_settings.master_gain_db);
  const float ceiling = db_to_linear(master_settings.limiter_ceiling_db);
  const float release_coefficient = std::exp(
      -1.0f / (0.001f * master_settings.limiter_release_ms *
               static_cast<float>(sample_rate)));
  float limiter_gain = 1.0f;
  double sum_squares = 0.0;
  float peak = 0.0f;

  for (float& sample : output.samples) {
    float value = sample * master_gain;
    if (master_settings.limiter_enabled) {
      const float absolute = std::fabs(value);
      const float target_gain = absolute > ceiling ? ceiling / absolute : 1.0f;
      if (target_gain < limiter_gain) {
        limiter_gain = target_gain;
      } else {
        limiter_gain = target_gain +
                       release_coefficient * (limiter_gain - target_gain);
      }
      value *= limiter_gain;
    }
    sample = std::clamp(value, -1.0f, 1.0f);
    peak = std::max(peak, std::fabs(sample));
    sum_squares += static_cast<double>(sample) * sample;
  }

  const float rms = output.samples.empty()
                        ? 0.0f
                        : static_cast<float>(std::sqrt(
                              sum_squares / output.samples.size()));
  result.status = AudioMixStatus::ready;
  result.metrics.peak_dbfs = linear_to_db(peak);
  result.metrics.rms_dbfs = linear_to_db(rms);
  result.metrics.integrated_lufs =
      rms > 1.0e-6f ? -0.691f + 10.0f * std::log10(rms * rms)
                    : kSilenceDb;
  result.metrics.digest = audio_buffer_digest(output);
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_mix_audio_f32(
    const float* const* track_samples,
    const std::uint64_t* track_frame_counts,
    const DigitorAudioTrackSettings* track_settings,
    std::uint32_t track_count,
    std::uint32_t sample_rate,
    std::uint32_t channels,
    float* output_samples,
    std::uint64_t output_frame_count,
    const DigitorAudioMasterSettings* master_settings,
    DigitorAudioMixMetrics* metrics) {
  if (!track_samples || !track_frame_counts || !track_settings ||
      track_count == 0u || sample_rate == 0u ||
      (channels != 1u && channels != 2u) || !output_samples ||
      output_frame_count == 0u || !master_settings || !metrics) {
    return 1u;
  }

  std::vector<digitor::AudioBuffer> tracks;
  std::vector<digitor::AudioTrackSettings> settings;
  tracks.reserve(track_count);
  settings.reserve(track_count);
  for (std::uint32_t index = 0; index < track_count; ++index) {
    if (!track_samples[index] || track_frame_counts[index] == 0u ||
        track_frame_counts[index] > output_frame_count) {
      return 1u;
    }
    digitor::AudioBuffer buffer;
    buffer.sample_rate = sample_rate;
    buffer.channels = channels;
    const std::size_t sample_count = static_cast<std::size_t>(
        track_frame_counts[index] * channels);
    buffer.samples.assign(track_samples[index],
                          track_samples[index] + sample_count);
    tracks.push_back(std::move(buffer));

    digitor::AudioTrackSettings native;
    native.gain_db = track_settings[index].gain_db;
    native.pan = track_settings[index].pan;
    native.fade_in_seconds = track_settings[index].fade_in_seconds;
    native.fade_out_seconds = track_settings[index].fade_out_seconds;
    native.mute = track_settings[index].mute != 0u;
    settings.push_back(native);
  }

  digitor::AudioMasterSettings master;
  master.master_gain_db = master_settings->master_gain_db;
  master.limiter_ceiling_db = master_settings->limiter_ceiling_db;
  master.limiter_release_ms = master_settings->limiter_release_ms;
  master.limiter_enabled = master_settings->limiter_enabled != 0u;

  digitor::AudioBuffer output;
  const auto result =
      digitor::mix_audio_reference(tracks, settings, output, master);
  if (result.status != digitor::AudioMixStatus::ready ||
      output.samples.size() >
          static_cast<std::size_t>(output_frame_count * channels)) {
    return 2u;
  }
  std::fill(output_samples,
            output_samples + static_cast<std::size_t>(output_frame_count * channels),
            0.0f);
  std::copy(output.samples.begin(), output.samples.end(), output_samples);
  metrics->peak_dbfs = result.metrics.peak_dbfs;
  metrics->rms_dbfs = result.metrics.rms_dbfs;
  metrics->integrated_lufs = result.metrics.integrated_lufs;
  metrics->digest = result.metrics.digest;
  return 0u;
}
