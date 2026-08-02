#include "digitor/professional_audio_engine.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace digitor {
namespace {
constexpr float kSilenceDb = -120.0f;
float db_to_gain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
float gain_to_db(float value) noexcept {
  return value <= 1.0e-9f ? kSilenceDb : 20.0f * std::log10(value);
}
float clamp_pan(float value) noexcept { return std::clamp(value, -1.0f, 1.0f); }
std::uint32_t channels_for(AudioChannelLayout layout) noexcept {
  return static_cast<std::uint32_t>(layout);
}
void clear(std::vector<std::vector<float>>& buffer, std::uint32_t frames) {
  for (auto& channel : buffer) std::fill_n(channel.data(), frames, 0.0f);
}
AudioBufferView mutable_view(std::vector<std::vector<float>>& buffer,
                             std::vector<float*>& pointers,
                             std::uint32_t frames) {
  for (std::size_t i = 0; i < buffer.size(); ++i) pointers[i] = buffer[i].data();
  return {pointers.data(), static_cast<std::uint32_t>(buffer.size()), frames};
}
ConstAudioBufferView const_view(const std::vector<std::vector<float>>& buffer,
                                std::vector<const float*>& pointers,
                                std::uint32_t frames) {
  for (std::size_t i = 0; i < buffer.size(); ++i) pointers[i] = buffer[i].data();
  return {pointers.data(), static_cast<std::uint32_t>(buffer.size()), frames};
}
void apply_pan(float& left, float& right, float pan) noexcept {
  const float p = clamp_pan(pan);
  const float left_gain = std::sqrt(0.5f * (1.0f - p));
  const float right_gain = std::sqrt(0.5f * (1.0f + p));
  left *= left_gain * 1.41421356237f;
  right *= right_gain * 1.41421356237f;
}
void apply_effects(std::vector<std::vector<float>>& buffer, std::uint32_t frames,
                   const std::vector<AudioEffectParameters>& effects) {
  for (const auto& effect : effects) {
    if (!effect.enabled) continue;
    if (effect.type == AudioEffectType::gain) {
      const float gain = db_to_gain(effect.gain_db);
      for (auto& channel : buffer)
        for (std::uint32_t i = 0; i < frames; ++i) channel[i] *= gain;
    } else if (effect.type == AudioEffectType::equalizer) {
      const float gain = db_to_gain((effect.low_gain_db + effect.mid_gain_db + effect.high_gain_db) / 3.0f);
      for (auto& channel : buffer)
        for (std::uint32_t i = 0; i < frames; ++i) channel[i] *= gain;
    } else if (effect.type == AudioEffectType::compressor) {
      const float threshold = db_to_gain(effect.threshold_db);
      const float ratio = std::max(1.0f, effect.ratio);
      for (auto& channel : buffer) {
        for (std::uint32_t i = 0; i < frames; ++i) {
          const float sample = channel[i];
          const float magnitude = std::abs(sample);
          if (magnitude > threshold) {
            const float compressed = threshold + (magnitude - threshold) / ratio;
            channel[i] = std::copysign(compressed, sample);
          }
        }
      }
    } else if (effect.type == AudioEffectType::limiter) {
      const float ceiling = db_to_gain(effect.ceiling_db);
      for (auto& channel : buffer)
        for (std::uint32_t i = 0; i < frames; ++i)
          channel[i] = std::clamp(channel[i], -ceiling, ceiling);
    }
  }
}
}  // namespace

float AudioAutomationLane::value_at(std::int64_t timeline_us) const noexcept {
  if (points.empty()) return default_value;
  if (timeline_us <= points.front().timeline_us) return points.front().value;
  if (timeline_us >= points.back().timeline_us) return points.back().value;
  const auto upper = std::upper_bound(points.begin(), points.end(), timeline_us,
      [](std::int64_t value, const AudioAutomationPoint& point) { return value < point.timeline_us; });
  const auto& right = *upper;
  const auto& left = *(upper - 1);
  if (left.curve == AudioAutomationCurve::hold || right.timeline_us == left.timeline_us)
    return left.value;
  const double t = static_cast<double>(timeline_us - left.timeline_us) /
                   static_cast<double>(right.timeline_us - left.timeline_us);
  return static_cast<float>(left.value + (right.value - left.value) * t);
}

struct ProfessionalAudioEngine::Impl {
  ProfessionalAudioConfig config;
  AudioSourceRender source;
  AudioPlaybackSink playback_sink;
  AudioExportSink export_sink;
  std::shared_ptr<const ProfessionalAudioSnapshot> snapshot;
  mutable std::mutex control_mutex;
  mutable std::mutex telemetry_mutex;
  ProfessionalAudioTelemetry counters;
  std::vector<std::vector<float>> source_buffer;
  std::vector<std::vector<float>> track_buffer;
  std::vector<std::vector<float>> master_buffer;
  std::vector<float*> mutable_pointers;
  std::vector<const float*> const_pointers;

  Impl(ProfessionalAudioConfig c, AudioSourceRender s, AudioPlaybackSink p, AudioExportSink e)
      : config(c), source(std::move(s)), playback_sink(std::move(p)), export_sink(std::move(e)) {}

  void allocate(std::uint32_t channels) {
    auto make = [&](std::vector<std::vector<float>>& target) {
      target.assign(channels, std::vector<float>(config.maximum_block_frames));
    };
    make(source_buffer); make(track_buffer); make(master_buffer);
    mutable_pointers.resize(channels);
    const_pointers.resize(channels);
  }

  DigitorResult render(std::int64_t start_us, std::uint32_t frames,
                       const AudioPlaybackSink& sink, std::string* diagnostic) noexcept {
    const auto started = std::chrono::steady_clock::now();
    std::shared_ptr<const ProfessionalAudioSnapshot> local;
    {
      std::scoped_lock lock(control_mutex);
      local = snapshot;
    }
    auto fail = [&](std::string message, bool source_failure) {
      std::scoped_lock lock(telemetry_mutex);
      if (source_failure) ++counters.source_failures; else ++counters.sink_failures;
      counters.last_error = std::move(message);
      if (diagnostic) *diagnostic = counters.last_error;
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    };
    if (!local) return fail("professional audio snapshot is not published", true);
    if (!source) return fail("professional audio source callback is missing", true);
    if (!sink) return fail("professional audio sink callback is missing", false);
    if (frames == 0 || frames > config.maximum_block_frames)
      return fail("audio block size exceeds configured maximum", true);

    const std::uint32_t channels = channels_for(local->layout);
    if (master_buffer.size() != channels) allocate(channels);
    clear(master_buffer, frames);
    const bool any_solo = std::any_of(local->tracks.begin(), local->tracks.end(),
                                     [](const AudioTrackState& track) { return track.enabled && track.solo; });

    for (const auto& track : local->tracks) {
      if (!track.enabled || track.muted || (any_solo && !track.solo)) continue;
      clear(track_buffer, frames);
      for (const auto& clip : track.clips) {
        if (!clip.enabled || start_us >= clip.timeline_out_us) continue;
        const std::int64_t block_duration_us = static_cast<std::int64_t>(
            (static_cast<std::uint64_t>(frames) * 1000000ull) / local->sample_rate);
        if (start_us + block_duration_us <= clip.timeline_in_us) continue;
        clear(source_buffer, frames);
        const std::int64_t source_start = clip.source_in_us + std::max<std::int64_t>(0, start_us - clip.timeline_in_us);
        std::string local_error;
        const auto result = source(clip, source_start, local->sample_rate,
                                   mutable_view(source_buffer, mutable_pointers, frames), local_error);
        if (result != DIGITOR_RESULT_OK)
          return fail(local_error.empty() ? "audio source render failed" : local_error, true);
        const float clip_gain = db_to_gain(clip.gain_db);
        for (std::uint32_t i = 0; i < frames; ++i) {
          const std::int64_t sample_us = start_us + static_cast<std::int64_t>(
              (static_cast<std::uint64_t>(i) * 1000000ull) / local->sample_rate);
          float fade = 1.0f;
          if (clip.fade_in_us > 0 && sample_us < clip.timeline_in_us + clip.fade_in_us)
            fade *= std::clamp(static_cast<float>(sample_us - clip.timeline_in_us) /
                              static_cast<float>(clip.fade_in_us), 0.0f, 1.0f);
          if (clip.fade_out_us > 0 && sample_us > clip.timeline_out_us - clip.fade_out_us)
            fade *= std::clamp(static_cast<float>(clip.timeline_out_us - sample_us) /
                              static_cast<float>(clip.fade_out_us), 0.0f, 1.0f);
          for (std::uint32_t c = 0; c < channels; ++c)
            track_buffer[c][i] += source_buffer[c][i] * clip_gain * fade;
        }
      }
      apply_effects(track_buffer, frames, track.effects);
      for (std::uint32_t i = 0; i < frames; ++i) {
        const std::int64_t sample_us = start_us + static_cast<std::int64_t>(
            (static_cast<std::uint64_t>(i) * 1000000ull) / local->sample_rate);
        const float automation_db = track.volume_automation.value_at(sample_us);
        const float gain = db_to_gain(track.gain_db + automation_db);
        const float pan = clamp_pan(track.pan + track.pan_automation.value_at(sample_us));
        if (channels >= 2) {
          float left = track_buffer[0][i] * gain;
          float right = track_buffer[1][i] * gain;
          apply_pan(left, right, pan);
          master_buffer[0][i] += left;
          master_buffer[1][i] += right;
          for (std::uint32_t c = 2; c < channels; ++c) master_buffer[c][i] += track_buffer[c][i] * gain;
        } else {
          master_buffer[0][i] += track_buffer[0][i] * gain;
        }
      }
    }

    const auto master = std::find_if(local->buses.begin(), local->buses.end(),
        [&](const AudioBusState& bus) { return bus.bus_id == local->master_bus_id; });
    if (master != local->buses.end() && master->enabled && !master->muted) {
      apply_effects(master_buffer, frames, master->effects);
      const float gain = db_to_gain(master->gain_db);
      for (std::uint32_t i = 0; i < frames; ++i) {
        if (channels >= 2) {
          float left = master_buffer[0][i] * gain;
          float right = master_buffer[1][i] * gain;
          apply_pan(left, right, master->pan);
          master_buffer[0][i] = left;
          master_buffer[1][i] = right;
        }
        for (std::uint32_t c = channels >= 2 ? 2u : 0u; c < channels; ++c)
          master_buffer[c][i] *= gain;
      }
    }

    double sum_squares = 0.0;
    float peak = 0.0f;
    std::uint64_t clipped = 0;
    for (const auto& channel : master_buffer) {
      for (std::uint32_t i = 0; i < frames; ++i) {
        const float magnitude = std::abs(channel[i]);
        peak = std::max(peak, magnitude);
        sum_squares += static_cast<double>(channel[i]) * channel[i];
        if (magnitude > 1.0f) ++clipped;
      }
    }
    std::string sink_error;
    const auto sink_result = sink(const_view(master_buffer, const_pointers, frames), start_us, sink_error);
    if (sink_result != DIGITOR_RESULT_OK)
      return fail(sink_error.empty() ? "professional audio sink failed" : sink_error, false);

    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    {
      std::scoped_lock lock(telemetry_mutex);
      ++counters.rendered_blocks;
      counters.rendered_frames += frames;
      counters.snapshot_revision = local->revision;
      counters.average_render_ms += (elapsed - counters.average_render_ms) /
                                    static_cast<double>(counters.rendered_blocks);
      const double sample_count = static_cast<double>(frames) * channels;
      const float rms = sample_count > 0.0 ? static_cast<float>(std::sqrt(sum_squares / sample_count)) : 0.0f;
      counters.master_meter.peak_db = gain_to_db(peak);
      counters.master_meter.rms_db = gain_to_db(rms);
      counters.master_meter.integrated_lufs = gain_to_db(rms) - 0.691f;
      counters.master_meter.clipped_samples += clipped;
      counters.last_error.clear();
    }
    if (diagnostic) diagnostic->clear();
    return DIGITOR_RESULT_OK;
  }
};

ProfessionalAudioEngine::ProfessionalAudioEngine(ProfessionalAudioConfig config,
    AudioSourceRender source, AudioPlaybackSink playback_sink, AudioExportSink export_sink)
    : impl_(std::make_unique<Impl>(config, std::move(source), std::move(playback_sink),
                                  std::move(export_sink))) {}
ProfessionalAudioEngine::~ProfessionalAudioEngine() = default;

DigitorResult ProfessionalAudioEngine::publish_snapshot(
    std::shared_ptr<const ProfessionalAudioSnapshot> snapshot, std::string* diagnostic) noexcept {
  if (!snapshot || snapshot->sample_rate == 0 || channels_for(snapshot->layout) == 0) {
    if (diagnostic) *diagnostic = "invalid professional audio snapshot";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (snapshot->tracks.size() > impl_->config.maximum_tracks ||
      snapshot->buses.size() > impl_->config.maximum_buses) {
    if (diagnostic) *diagnostic = "professional audio snapshot exceeds configured limits";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  {
    std::scoped_lock lock(impl_->control_mutex);
    impl_->snapshot = std::move(snapshot);
  }
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}
DigitorResult ProfessionalAudioEngine::render_playback(
    std::int64_t start, std::uint32_t frames, std::string* diagnostic) noexcept {
  return impl_->render(start, frames, impl_->playback_sink, diagnostic);
}
DigitorResult ProfessionalAudioEngine::render_export(
    std::int64_t start, std::uint32_t frames, std::string* diagnostic) noexcept {
  return impl_->render(start, frames, impl_->export_sink, diagnostic);
}
void ProfessionalAudioEngine::notify_underrun() noexcept {
  std::scoped_lock lock(impl_->telemetry_mutex); ++impl_->counters.underruns;
}
void ProfessionalAudioEngine::notify_overrun() noexcept {
  std::scoped_lock lock(impl_->telemetry_mutex); ++impl_->counters.overruns;
}
ProfessionalAudioTelemetry ProfessionalAudioEngine::telemetry() const {
  std::scoped_lock lock(impl_->telemetry_mutex); return impl_->counters;
}

}  // namespace digitor
