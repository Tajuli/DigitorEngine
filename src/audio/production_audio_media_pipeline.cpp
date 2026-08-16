#include "digitor/production_audio_media_pipeline.hpp"

#include "digitor/media.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace digitor {
namespace {

AudioChannelLayout channel_layout(std::uint32_t channels) {
  switch (channels) {
    case 1: return AudioChannelLayout::mono;
    case 2: return AudioChannelLayout::stereo;
    case 6: return AudioChannelLayout::surround_5_1;
    case 8: return AudioChannelLayout::surround_7_1;
    default: throw std::invalid_argument("unsupported production audio channel layout");
  }
}

std::int64_t frame_duration_us(const AudioFrame& frame) noexcept {
  if (frame.duration > 0) return frame.duration;
  if (!frame.sample_rate || !frame.channels) return 0;
  const auto sample_frames = frame.samples.size() / frame.channels;
  return static_cast<std::int64_t>(
      (static_cast<long double>(sample_frames) * 1'000'000.0L) /
      static_cast<long double>(frame.sample_rate));
}

bool no_audio_stream_error(const std::exception& error) {
  return std::string(error.what()).find("no decodable audio stream") !=
         std::string::npos;
}

std::mutex& registry_mutex() {
  static std::mutex value;
  return value;
}

std::unordered_map<std::string, std::weak_ptr<ProductionAudioMediaPipeline>>&
registry() {
  static std::unordered_map<std::string,
                            std::weak_ptr<ProductionAudioMediaPipeline>> value;
  return value;
}

}  // namespace

struct ProductionAudioMediaPipeline::Impl {
  explicit Impl(std::string source_path, std::unique_ptr<AudioDecoder> source_decoder,
                std::shared_ptr<AudioFrame> probe)
      : media_path(std::move(source_path)), decoder(std::move(source_decoder)) {
    if (!decoder || !probe || !probe->sample_rate || !probe->channels ||
        probe->samples.empty() || probe->samples.size() % probe->channels != 0) {
      throw std::invalid_argument("invalid production audio decoder probe");
    }
    sample_rate = probe->sample_rate;
    channels = probe->channels;
    layout = channel_layout(channels);
    decoder->seek(0);

    ProfessionalAudioConfig config{};
    config.maximum_block_frames = ProductionAudioMediaPipeline::maximum_block_frames();
    config.require_realtime_safe_render = false;

    auto source = [this](const AudioClipState& clip,
                         std::int64_t source_start_us,
                         std::uint32_t requested_sample_rate,
                         AudioBufferView destination,
                         std::string& diagnostic) {
      return render_source(clip, source_start_us, requested_sample_rate,
                           destination, diagnostic);
    };
    auto playback = [this](ConstAudioBufferView source,
                           std::int64_t timeline_start_us,
                           std::string& diagnostic) {
      if (!active_playback_sink) {
        diagnostic = "production playback sink is not attached";
        return DIGITOR_RESULT_NOT_INITIALIZED;
      }
      return active_playback_sink(source, timeline_start_us, diagnostic);
    };
    auto exported = [this](ConstAudioBufferView source,
                           std::int64_t timeline_start_us,
                           std::string& diagnostic) {
      if (!active_export_sink) {
        diagnostic = "production export audio sink is not attached";
        return DIGITOR_RESULT_NOT_INITIALIZED;
      }
      return active_export_sink(source, timeline_start_us, diagnostic);
    };

    engine = std::make_unique<ProfessionalAudioEngine>(
        config, std::move(source), std::move(playback), std::move(exported));
  }

  DigitorResult render_source(const AudioClipState&,
                              std::int64_t source_start_us,
                              std::uint32_t requested_sample_rate,
                              AudioBufferView destination,
                              std::string& diagnostic) noexcept {
    if (!decoder || source_start_us < 0 || requested_sample_rate != sample_rate ||
        destination.frame_count == 0 || destination.channel_count != channels ||
        !destination.channels) {
      diagnostic = "production audio source request does not match canonical format";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    for (std::uint32_t channel = 0; channel < destination.channel_count; ++channel) {
      if (!destination.channels[channel]) {
        diagnostic = "production audio destination channel is null";
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      std::fill_n(destination.channels[channel], destination.frame_count, 0.0f);
    }

    try {
      decoder->seek(source_start_us);
      const auto block_duration = static_cast<std::int64_t>(
          (static_cast<long double>(destination.frame_count) * 1'000'000.0L) /
          static_cast<long double>(sample_rate));
      const auto max_value = (std::numeric_limits<std::int64_t>::max)();
      const auto block_end = source_start_us > max_value - block_duration
                                 ? max_value
                                 : source_start_us + block_duration;

      FrameNumber frame_number = 0;
      bool copied_any = false;
      for (std::uint32_t decoded = 0; decoded < 4096; ++decoded) {
        auto frame = decoder->decode(frame_number++);
        if (!frame) break;
        if (frame->sample_rate != sample_rate || frame->channels != channels ||
            frame->channels == 0 ||
            frame->samples.size() % frame->channels != 0) {
          diagnostic = "production audio source format changed during render";
          return DIGITOR_RESULT_UNSUPPORTED;
        }

        const auto source_frames = frame->samples.size() / frame->channels;
        if (source_frames == 0) continue;
        const auto duration = frame_duration_us(*frame);
        if (duration <= 0) continue;
        const auto frame_end = frame->pts > max_value - duration
                                   ? max_value
                                   : frame->pts + duration;
        if (frame_end <= source_start_us) continue;
        if (frame->pts >= block_end) break;

        const auto overlap_start = (std::max)(source_start_us, frame->pts);
        const auto overlap_end = (std::min)(block_end, frame_end);
        if (overlap_end <= overlap_start) continue;

        auto source_offset = static_cast<std::uint64_t>(
            (static_cast<long double>(overlap_start - frame->pts) * sample_rate) /
            1'000'000.0L);
        auto destination_offset = static_cast<std::uint64_t>(
            (static_cast<long double>(overlap_start - source_start_us) * sample_rate) /
            1'000'000.0L);
        auto overlap_frames = static_cast<std::uint64_t>(
            (static_cast<long double>(overlap_end - overlap_start) * sample_rate) /
            1'000'000.0L);
        if (overlap_frames == 0) overlap_frames = 1;
        source_offset = (std::min<std::uint64_t>)(source_offset, source_frames);
        destination_offset = (std::min<std::uint64_t>)(
            destination_offset, destination.frame_count);
        const auto available_source = source_frames - source_offset;
        const auto available_destination =
            static_cast<std::uint64_t>(destination.frame_count) - destination_offset;
        overlap_frames = (std::min)({overlap_frames, available_source,
                                     available_destination});
        if (overlap_frames == 0) continue;

        for (std::uint32_t channel = 0; channel < channels; ++channel) {
          auto* output = destination.channels[channel] + destination_offset;
          for (std::uint64_t index = 0; index < overlap_frames; ++index) {
            output[index] = frame->samples[
                (source_offset + index) * channels + channel];
          }
        }
        copied_any = true;
        if (destination_offset + overlap_frames >= destination.frame_count) break;
      }
      had_source_audio = had_source_audio || copied_any;
      diagnostic.clear();
      return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
      diagnostic = "out of memory rendering canonical production audio";
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (const std::exception& error) {
      diagnostic = std::string("production audio decode failed: ") + error.what();
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    } catch (...) {
      diagnostic = "production audio decode failed";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  }

  DigitorResult publish(std::uint64_t new_revision,
                        std::int64_t new_duration_us,
                        double master_gain_db,
                        bool enable_dynamics,
                        std::string* diagnostic) noexcept {
    if (!new_revision || new_duration_us < 0 || !std::isfinite(master_gain_db) ||
        master_gain_db < -120.0 || master_gain_db > 24.0) {
      if (diagnostic) *diagnostic = "invalid canonical production audio snapshot";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    try {
      auto snapshot = std::make_shared<ProfessionalAudioSnapshot>();
      snapshot->revision = new_revision;
      snapshot->sample_rate = sample_rate;
      snapshot->layout = layout;
      snapshot->master_bus_id = 1;

      AudioTrackState track{};
      track.track_id = 1;
      track.bus_id = 1;
      track.volume_automation.default_value = 0.0f;
      track.pan_automation.default_value = 0.0f;
      AudioClipState clip{};
      clip.clip_id = 1;
      clip.timeline_in_us = 0;
      clip.timeline_out_us = new_duration_us > 0
                                 ? new_duration_us
                                 : (std::numeric_limits<std::int64_t>::max)() / 4;
      clip.source_in_us = 0;
      track.clips.push_back(clip);
      snapshot->tracks.push_back(std::move(track));

      AudioBusState master{};
      master.bus_id = 1;
      master.gain_db = static_cast<float>(master_gain_db);
      // Keep the legacy dynamics toggle neutral until a concrete timeline
      // effect snapshot is published. Both sinks still traverse one graph.
      (void)enable_dynamics;
      snapshot->buses.push_back(std::move(master));

      const auto result = engine->publish_snapshot(snapshot, diagnostic);
      if (result == DIGITOR_RESULT_OK) {
        revision_value = new_revision;
        duration_value = new_duration_us;
      }
      return result;
    } catch (const std::bad_alloc&) {
      if (diagnostic) *diagnostic = "out of memory publishing production audio snapshot";
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
      if (diagnostic) *diagnostic = "failed to publish production audio snapshot";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
  }

  template <typename Sink>
  DigitorResult render(bool playback, std::int64_t timeline_start_us,
                       std::uint32_t frame_count, Sink sink,
                       bool* out_had_source_audio,
                       std::string* diagnostic) noexcept {
    if (out_had_source_audio) *out_had_source_audio = false;
    if (timeline_start_us < 0 || frame_count == 0 ||
        frame_count > ProductionAudioMediaPipeline::maximum_block_frames() ||
        !sink) {
      if (diagnostic) *diagnostic = "invalid production audio render request";
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(render_mutex);
    if (!playback && !export_revision_locked) {
      if (diagnostic) *diagnostic = "production audio export revision is not frozen";
      return DIGITOR_RESULT_NOT_INITIALIZED;
    }
    had_source_audio = false;
    if (playback) active_playback_sink = std::move(sink);
    else active_export_sink = std::move(sink);
    const auto result = playback
        ? engine->render_playback(timeline_start_us, frame_count, diagnostic)
        : engine->render_export(timeline_start_us, frame_count, diagnostic);
    if (playback) active_playback_sink = {};
    else active_export_sink = {};
    if (out_had_source_audio) *out_had_source_audio = had_source_audio;
    return result;
  }

  std::string media_path;
  std::unique_ptr<AudioDecoder> decoder;
  std::unique_ptr<ProfessionalAudioEngine> engine;
  AudioChannelLayout layout{AudioChannelLayout::stereo};
  std::uint32_t sample_rate{};
  std::uint32_t channels{};
  mutable std::mutex render_mutex;
  AudioPlaybackSink active_playback_sink;
  AudioExportSink active_export_sink;
  bool had_source_audio{};
  bool export_revision_locked{};
  std::uint64_t revision_value{1};
  std::int64_t duration_value{};
};

ProductionAudioMediaPipeline::ProductionAudioMediaPipeline(
    std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
ProductionAudioMediaPipeline::~ProductionAudioMediaPipeline() = default;

std::uint32_t ProductionAudioMediaPipeline::sample_rate() const noexcept {
  return impl_ ? impl_->sample_rate : 0;
}
std::uint32_t ProductionAudioMediaPipeline::channels() const noexcept {
  return impl_ ? impl_->channels : 0;
}
std::uint64_t ProductionAudioMediaPipeline::revision() const noexcept {
  if (!impl_) return 0;
  std::scoped_lock lock(impl_->render_mutex);
  return impl_->revision_value;
}
std::int64_t ProductionAudioMediaPipeline::duration_us() const noexcept {
  if (!impl_) return 0;
  std::scoped_lock lock(impl_->render_mutex);
  return impl_->duration_value;
}

DigitorResult ProductionAudioMediaPipeline::publish_single_source_snapshot(
    std::uint64_t revision, std::int64_t duration_us, double master_gain_db,
    bool enable_dynamics, std::string* diagnostic) noexcept {
  if (!impl_) return DIGITOR_RESULT_NOT_INITIALIZED;
  std::scoped_lock lock(impl_->render_mutex);
  if (impl_->export_revision_locked) {
    if (diagnostic) *diagnostic =
        "production audio snapshot is frozen by an active export";
    return DIGITOR_RESULT_RESOURCE_IN_USE;
  }
  return impl_->publish(revision, duration_us, master_gain_db,
                        enable_dynamics, diagnostic);
}

DigitorResult ProductionAudioMediaPipeline::begin_export_revision(
    std::uint64_t revision, std::string* diagnostic) noexcept {
  if (!impl_ || !revision) return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::scoped_lock lock(impl_->render_mutex);
  if (impl_->export_revision_locked) {
    if (diagnostic) *diagnostic = "production audio export is already active";
    return DIGITOR_RESULT_RESOURCE_IN_USE;
  }
  if (impl_->revision_value != revision) {
    if (diagnostic) *diagnostic =
        "production audio revision differs from frozen export snapshot";
    return DIGITOR_RESULT_RESOURCE_IN_USE;
  }
  impl_->export_revision_locked = true;
  if (diagnostic) diagnostic->clear();
  return DIGITOR_RESULT_OK;
}

void ProductionAudioMediaPipeline::end_export_revision() noexcept {
  if (!impl_) return;
  std::scoped_lock lock(impl_->render_mutex);
  impl_->active_export_sink = {};
  impl_->export_revision_locked = false;
}

DigitorResult ProductionAudioMediaPipeline::render_playback(
    std::int64_t start, std::uint32_t frames, AudioPlaybackSink sink,
    bool* had_audio, std::string* diagnostic) noexcept {
  if (!impl_) return DIGITOR_RESULT_NOT_INITIALIZED;
  return impl_->render(true, start, frames, std::move(sink), had_audio,
                       diagnostic);
}

DigitorResult ProductionAudioMediaPipeline::render_export(
    std::int64_t start, std::uint32_t frames, AudioExportSink sink,
    bool* had_audio, std::string* diagnostic) noexcept {
  if (!impl_) return DIGITOR_RESULT_NOT_INITIALIZED;
  return impl_->render(false, start, frames, std::move(sink), had_audio,
                       diagnostic);
}

ProfessionalAudioTelemetry ProductionAudioMediaPipeline::telemetry() const {
  if (!impl_ || !impl_->engine) return {};
  std::scoped_lock lock(impl_->render_mutex);
  return impl_->engine->telemetry();
}

ProductionAudioPipelineAcquireResult
acquire_production_audio_media_pipeline(const std::string& media_path) noexcept {
  ProductionAudioPipelineAcquireResult result{};
  if (media_path.empty()) {
    result.result = DIGITOR_RESULT_INVALID_ARGUMENT;
    result.diagnostic = "production audio media path is empty";
    return result;
  }

  try {
    {
      std::scoped_lock lock(registry_mutex());
      const auto found = registry().find(media_path);
      if (found != registry().end()) {
        if (auto existing = found->second.lock()) {
          result.pipeline = std::move(existing);
          result.result = DIGITOR_RESULT_OK;
          return result;
        }
        registry().erase(found);
      }
    }

    std::unique_ptr<AudioDecoder> decoder;
    std::shared_ptr<AudioFrame> probe;
    try {
      decoder = open_audio_decoder(media_path);
      probe = decoder ? decoder->decode(0) : nullptr;
    } catch (const std::exception& error) {
      if (no_audio_stream_error(error)) {
        result.result = DIGITOR_RESULT_OK;
        result.no_audio_stream = true;
        result.diagnostic.clear();
        return result;
      }
      result.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      result.diagnostic = std::string("production audio decoder unavailable: ") +
                          error.what();
      return result;
    }
    if (!decoder || !probe) {
      result.result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      result.diagnostic = "production audio decoder returned no probe frame";
      return result;
    }

    auto impl = std::make_unique<ProductionAudioMediaPipeline::Impl>(
        media_path, std::move(decoder), std::move(probe));
    auto pipeline = std::shared_ptr<ProductionAudioMediaPipeline>(
        new ProductionAudioMediaPipeline(std::move(impl)));
    std::string diagnostic;
    const auto published = pipeline->publish_single_source_snapshot(
        1, 0, 0.0, false, &diagnostic);
    if (published != DIGITOR_RESULT_OK) {
      result.result = published;
      result.diagnostic = std::move(diagnostic);
      return result;
    }

    {
      std::scoped_lock lock(registry_mutex());
      const auto found = registry().find(media_path);
      if (found != registry().end()) {
        if (auto existing = found->second.lock()) pipeline = std::move(existing);
      }
      registry()[media_path] = pipeline;
    }

    result.pipeline = std::move(pipeline);
    result.result = DIGITOR_RESULT_OK;
    return result;
  } catch (const std::bad_alloc&) {
    result.result = DIGITOR_RESULT_OUT_OF_MEMORY;
    result.diagnostic = "out of memory creating canonical production audio pipeline";
    return result;
  } catch (const std::exception& error) {
    result.result = DIGITOR_RESULT_UNSUPPORTED;
    result.diagnostic = error.what();
    return result;
  } catch (...) {
    result.result = DIGITOR_RESULT_INTERNAL_ERROR;
    result.diagnostic = "failed to create canonical production audio pipeline";
    return result;
  }
}

}  // namespace digitor
