#include "digitor/timeline_audio_session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmreg.h>
#include <xaudio2.h>

#include "digitor/media.hpp"
#include "digitor/production_audio_media_pipeline.hpp"
#endif

namespace {

using PlaybackClock = std::chrono::steady_clock;

#if defined(_WIN32)

class PipelinePlaybackDecoder final : public digitor::AudioDecoder {
public:
    explicit PipelinePlaybackDecoder(
        std::shared_ptr<digitor::ProductionAudioMediaPipeline> pipeline)
        : pipeline_(std::move(pipeline)) {
        if (!pipeline_ || !pipeline_->sample_rate() || !pipeline_->channels())
            throw std::invalid_argument("canonical production audio pipeline is required");
    }

    std::shared_ptr<digitor::AudioFrame> decode(digitor::FrameNumber) override {
        constexpr std::uint32_t kBlockFrames = 1024;
        auto frame = std::make_shared<digitor::AudioFrame>();
        frame->sample_rate = pipeline_->sample_rate();
        frame->channels = pipeline_->channels();
        frame->pts = position_us_;
        frame->duration = static_cast<std::int64_t>(
            (static_cast<std::uint64_t>(kBlockFrames) * 1'000'000ULL) /
            frame->sample_rate);

        bool had_source_audio = false;
        std::string diagnostic;
        const auto result = pipeline_->render_playback(
            position_us_, kBlockFrames,
            [frame](digitor::ConstAudioBufferView source, std::int64_t,
                    std::string& sink_diagnostic) {
                if (!source.channels || source.channel_count != frame->channels ||
                    source.frame_count != kBlockFrames) {
                    sink_diagnostic = "canonical playback block format changed";
                    return DIGITOR_RESULT_UNSUPPORTED;
                }
                try {
                    frame->samples.assign(
                        static_cast<std::size_t>(source.frame_count) * source.channel_count,
                        0.0f);
                    for (std::uint32_t sample = 0; sample < source.frame_count; ++sample) {
                        for (std::uint32_t channel = 0; channel < source.channel_count; ++channel) {
                            if (!source.channels[channel]) {
                                sink_diagnostic = "canonical playback channel is null";
                                return DIGITOR_RESULT_INTERNAL_ERROR;
                            }
                            frame->samples[static_cast<std::size_t>(sample) * source.channel_count + channel] =
                                source.channels[channel][sample];
                        }
                    }
                    sink_diagnostic.clear();
                    return DIGITOR_RESULT_OK;
                } catch (const std::bad_alloc&) {
                    sink_diagnostic = "out of memory interleaving canonical playback block";
                    return DIGITOR_RESULT_OUT_OF_MEMORY;
                } catch (...) {
                    sink_diagnostic = "failed to interleave canonical playback block";
                    return DIGITOR_RESULT_INTERNAL_ERROR;
                }
            },
            &had_source_audio, &diagnostic);
        if (result != DIGITOR_RESULT_OK)
            throw std::runtime_error(diagnostic.empty()
                                         ? "canonical playback render failed"
                                         : diagnostic);
        if (!had_source_audio) return {};
        if (position_us_ > (std::numeric_limits<std::int64_t>::max)() - frame->duration)
            throw std::overflow_error("canonical playback timestamp overflow");
        position_us_ += frame->duration;
        return frame;
    }

    void seek(std::int64_t timestamp) override {
        if (timestamp < 0) throw std::invalid_argument("negative canonical audio seek");
        position_us_ = timestamp;
    }

    digitor::DecoderInfo info() const override {
        digitor::DecoderInfo value{};
        value.selected = digitor::HardwareDecode::automatic;
        value.hardware_accelerated = false;
        value.implementation = "Digitor ProductionAudioMediaPipeline";
        value.native_surface_output = false;
        return value;
    }

private:
    std::shared_ptr<digitor::ProductionAudioMediaPipeline> pipeline_;
    std::int64_t position_us_{};
};

class WindowsPreviewAudioOutput final {
public:
    explicit WindowsPreviewAudioOutput(std::unique_ptr<digitor::AudioDecoder> decoder)
        : decoder_(std::move(decoder)) {
        if (!decoder_) throw std::invalid_argument("audio decoder is required");
        initialize_xaudio();
        worker_ = std::thread([this] { worker_loop(); });
    }

    ~WindowsPreviewAudioOutput() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutting_down_ = true;
            playing_ = false;
            if (source_voice_) source_voice_->Stop(0);
        }
        condition_.notify_all();
        if (worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        destroy_source_voice_locked();
        if (mastering_voice_) {
            mastering_voice_->DestroyVoice();
            mastering_voice_ = nullptr;
        }
        if (xaudio_) {
            xaudio_->Release();
            xaudio_ = nullptr;
        }
        if (xaudio_module_) {
            FreeLibrary(xaudio_module_);
            xaudio_module_ = nullptr;
        }
    }

    WindowsPreviewAudioOutput(const WindowsPreviewAudioOutput&) = delete;
    WindowsPreviewAudioOutput& operator=(const WindowsPreviewAudioOutput&) = delete;

    DigitorResult prime(std::int64_t position_us, double playback_rate) {
        std::lock_guard<std::mutex> lock(mutex_);
        playback_rate_ = playback_rate;
        return seek_locked(position_us, false);
    }

    DigitorResult play() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failure_ != DIGITOR_RESULT_OK) return failure_;
        if (source_voice_) {
            const HRESULT result = source_voice_->Start(0);
            if (FAILED(result)) {
                failure_ = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
                return failure_;
            }
        }
        playing_ = true;
        condition_.notify_all();
        return DIGITOR_RESULT_OK;
    }

    DigitorResult pause() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failure_ != DIGITOR_RESULT_OK) return failure_;
        playing_ = false;
        if (source_voice_) {
            const HRESULT result = source_voice_->Stop(0);
            if (FAILED(result)) {
                failure_ = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
                return failure_;
            }
        }
        return DIGITOR_RESULT_OK;
    }

    DigitorResult stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failure_ != DIGITOR_RESULT_OK) return failure_;
        playing_ = false;
        if (source_voice_) source_voice_->Stop(0);
        return seek_locked(0, false);
    }

    DigitorResult seek(std::int64_t position_us) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failure_ != DIGITOR_RESULT_OK) return failure_;
        const bool resume = playing_;
        if (source_voice_) source_voice_->Stop(0);
        const auto result = seek_locked(position_us, resume);
        if (result == DIGITOR_RESULT_OK) condition_.notify_all();
        return result;
    }

    DigitorResult set_playback_rate(double playback_rate) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failure_ != DIGITOR_RESULT_OK) return failure_;
        playback_rate_ = playback_rate;
        return apply_controls_locked();
    }

private:
    using XAudio2CreateFn = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
    static constexpr std::size_t kQueueDepth = 4;

    void initialize_xaudio() {
        xaudio_module_ = LoadLibraryW(L"xaudio2_9.dll");
        if (!xaudio_module_)
            throw std::runtime_error("Windows XAudio2 2.9 runtime is unavailable");
        const auto create = reinterpret_cast<XAudio2CreateFn>(
            GetProcAddress(xaudio_module_, "XAudio2Create"));
        if (!create) throw std::runtime_error("XAudio2Create export is unavailable");
        HRESULT result = create(&xaudio_, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(result) || !xaudio_)
            throw std::runtime_error("XAudio2 engine creation failed");
        result = xaudio_->CreateMasteringVoice(&mastering_voice_);
        if (FAILED(result) || !mastering_voice_)
            throw std::runtime_error("XAudio2 mastering voice creation failed");
    }

    void destroy_source_voice_locked() noexcept {
        if (source_voice_) {
            source_voice_->Stop(0);
            source_voice_->FlushSourceBuffers();
            source_voice_->DestroyVoice();
            source_voice_ = nullptr;
        }
        queued_frames_.clear();
        source_sample_rate_ = 0;
        source_channels_ = 0;
    }

    DigitorResult create_source_voice_locked(const digitor::AudioFrame& frame) {
        if (!xaudio_ || frame.sample_rate < 8000 || frame.sample_rate > 384000 ||
            frame.channels == 0 || frame.channels > 8) {
            return DIGITOR_RESULT_UNSUPPORTED;
        }
        destroy_source_voice_locked();
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = static_cast<WORD>(frame.channels);
        format.nSamplesPerSec = frame.sample_rate;
        format.wBitsPerSample = 32;
        format.nBlockAlign = static_cast<WORD>(frame.channels * sizeof(float));
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        format.cbSize = 0;
        const HRESULT result = xaudio_->CreateSourceVoice(
            &source_voice_, &format, 0, 4.0f, nullptr, nullptr, nullptr);
        if (FAILED(result) || !source_voice_) {
            source_voice_ = nullptr;
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        }
        source_sample_rate_ = frame.sample_rate;
        source_channels_ = frame.channels;
        return apply_controls_locked();
    }

    DigitorResult apply_controls_locked() {
        if (!source_voice_) return DIGITOR_RESULT_OK;
        if (FAILED(source_voice_->SetVolume(1.0f)))
            return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        if (FAILED(source_voice_->SetFrequencyRatio(static_cast<float>(playback_rate_))))
            return DIGITOR_RESULT_UNSUPPORTED;
        return DIGITOR_RESULT_OK;
    }

    static std::int64_t frame_duration_us(const digitor::AudioFrame& frame) noexcept {
        if (frame.duration > 0) return frame.duration;
        if (frame.sample_rate == 0 || frame.channels == 0) return 0;
        const auto sample_frames = frame.samples.size() / frame.channels;
        return static_cast<std::int64_t>(
            (static_cast<long double>(sample_frames) * 1000000.0L) /
            static_cast<long double>(frame.sample_rate));
    }

    std::shared_ptr<digitor::AudioFrame> decode_for_position_locked(std::int64_t position_us) {
        for (;;) {
            auto frame = decoder_->decode(next_frame_number_++);
            if (!frame) return {};
            if (frame->channels == 0 || frame->sample_rate == 0 ||
                frame->samples.size() % frame->channels != 0) {
                throw std::runtime_error("decoded audio frame has invalid PCM layout");
            }
            const auto duration_us = frame_duration_us(*frame);
            const auto frame_end = frame->pts > (std::numeric_limits<std::int64_t>::max)() - duration_us
                                       ? (std::numeric_limits<std::int64_t>::max)()
                                       : frame->pts + duration_us;
            if (duration_us > 0 && frame_end <= position_us) continue;
            if (position_us > frame->pts && !frame->samples.empty()) {
                const auto delta_us = position_us - frame->pts;
                const auto total_sample_frames = frame->samples.size() / frame->channels;
                auto trim_sample_frames = static_cast<std::size_t>(
                    (static_cast<long double>(delta_us) * frame->sample_rate) / 1000000.0L);
                trim_sample_frames = (std::min)(trim_sample_frames, total_sample_frames);
                const auto trim_samples = trim_sample_frames * frame->channels;
                if (trim_samples >= frame->samples.size()) continue;
                frame->samples.erase(
                    frame->samples.begin(), frame->samples.begin() + static_cast<std::ptrdiff_t>(trim_samples));
                const auto trimmed_us = static_cast<std::int64_t>(
                    (static_cast<long double>(trim_sample_frames) * 1000000.0L) /
                    static_cast<long double>(frame->sample_rate));
                frame->pts += trimmed_us;
                frame->duration = frame_duration_us(*frame);
            }
            return frame;
        }
    }

    DigitorResult submit_frame_locked(std::shared_ptr<digitor::AudioFrame> frame) {
        if (!frame || frame->samples.empty()) return DIGITOR_RESULT_OK;
        if (!source_voice_) {
            const auto create_result = create_source_voice_locked(*frame);
            if (create_result != DIGITOR_RESULT_OK) return create_result;
        }
        if (frame->sample_rate != source_sample_rate_ || frame->channels != source_channels_)
            return DIGITOR_RESULT_UNSUPPORTED;
        const auto byte_count = frame->samples.size() * sizeof(float);
        if (byte_count > (std::numeric_limits<UINT32>::max)())
            return DIGITOR_RESULT_UNSUPPORTED;
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(byte_count);
        buffer.pAudioData = reinterpret_cast<const BYTE*>(frame->samples.data());
        const HRESULT result = source_voice_->SubmitSourceBuffer(&buffer);
        if (FAILED(result)) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        queued_frames_.push_back(std::move(frame));
        return DIGITOR_RESULT_OK;
    }

    void reap_consumed_locked() noexcept {
        if (!source_voice_) {
            queued_frames_.clear();
            return;
        }
        XAUDIO2_VOICE_STATE state{};
        source_voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        while (queued_frames_.size() > state.BuffersQueued) queued_frames_.pop_front();
    }

    DigitorResult seek_locked(std::int64_t position_us, bool resume) {
        try {
            destroy_source_voice_locked();
            decoder_->seek(position_us);
            next_frame_number_ = 0;
            eof_ = false;
            auto first = decode_for_position_locked(position_us);
            if (!first) {
                eof_ = true;
                playing_ = resume;
                return DIGITOR_RESULT_OK;
            }
            auto result = create_source_voice_locked(*first);
            if (result != DIGITOR_RESULT_OK) return result;
            result = submit_frame_locked(std::move(first));
            if (result != DIGITOR_RESULT_OK) return result;
            playing_ = resume;
            if (resume && source_voice_) {
                if (FAILED(source_voice_->Start(0))) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
            }
            return DIGITOR_RESULT_OK;
        } catch (const std::bad_alloc&) {
            return DIGITOR_RESULT_OUT_OF_MEMORY;
        } catch (...) {
            return DIGITOR_RESULT_INTERNAL_ERROR;
        }
    }

    void worker_loop() noexcept {
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] { return shutting_down_ || playing_; });
                if (shutting_down_) return;
                reap_consumed_locked();
                if (failure_ == DIGITOR_RESULT_OK && !eof_ && source_voice_ &&
                    queued_frames_.size() < kQueueDepth) {
                    try {
                        auto frame = decoder_->decode(next_frame_number_++);
                        if (!frame) {
                            eof_ = true;
                        } else {
                            const auto result = submit_frame_locked(std::move(frame));
                            if (result != DIGITOR_RESULT_OK) {
                                failure_ = result;
                                playing_ = false;
                                source_voice_->Stop(0);
                            }
                        }
                    } catch (const std::bad_alloc&) {
                        failure_ = DIGITOR_RESULT_OUT_OF_MEMORY;
                        playing_ = false;
                        if (source_voice_) source_voice_->Stop(0);
                    } catch (...) {
                        failure_ = DIGITOR_RESULT_INTERNAL_ERROR;
                        playing_ = false;
                        if (source_voice_) source_voice_->Stop(0);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }

    std::unique_ptr<digitor::AudioDecoder> decoder_;
    HMODULE xaudio_module_{};
    IXAudio2* xaudio_{};
    IXAudio2MasteringVoice* mastering_voice_{};
    IXAudio2SourceVoice* source_voice_{};
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::shared_ptr<digitor::AudioFrame>> queued_frames_;
    digitor::FrameNumber next_frame_number_{};
    std::uint32_t source_sample_rate_{};
    std::uint32_t source_channels_{};
    double playback_rate_{1.0};
    DigitorResult failure_{DIGITOR_RESULT_OK};
    bool playing_{};
    bool eof_{};
    bool shutting_down_{};
};

#endif

bool valid_config(const DigitorTimelineSessionConfig& config) noexcept {
    return config.sample_rate >= 8000 && config.sample_rate <= 384000 &&
           config.channels > 0 && config.channels <= 8 && config.duration_us >= 0;
}

bool valid_controls(const DigitorAudioSessionControls& controls) noexcept {
    return std::isfinite(controls.master_gain_db) && controls.master_gain_db >= -120.0 &&
           controls.master_gain_db <= 24.0 && std::isfinite(controls.playback_rate) &&
           controls.playback_rate >= 0.25 && controls.playback_rate <= 4.0;
}

} // namespace

struct DigitorTimelineAudioSession {
    std::mutex mutex;
    DigitorTimelineSessionStatus status{};
    DigitorTimelineSessionTelemetry telemetry{};
    PlaybackClock::time_point playback_anchor_time{};
    int64_t playback_anchor_position_us = 0;
    bool playback_anchor_valid = false;
#if defined(_WIN32)
    std::shared_ptr<digitor::ProductionAudioMediaPipeline> audio_pipeline;
    std::unique_ptr<WindowsPreviewAudioOutput> preview_audio;
#endif
};

namespace {

[[maybe_unused]] std::uint64_t audio_revision_locked(const DigitorTimelineAudioSession* session) noexcept {
    return session ? session->telemetry.control_updates + 1 : 0;
}

void anchor_playback_locked(
    DigitorTimelineAudioSession* session,
    PlaybackClock::time_point now) noexcept {
    session->playback_anchor_position_us = session->status.position_us;
    session->playback_anchor_time = now;
    session->playback_anchor_valid = true;
}

void materialize_playback_position_locked(
    DigitorTimelineAudioSession* session,
    PlaybackClock::time_point now) noexcept {
    if (session->status.playback_state != DIGITOR_PLAYBACK_PLAYING ||
        !session->playback_anchor_valid) {
        return;
    }

    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                now - session->playback_anchor_time)
                                .count();
    if (elapsed_us <= 0) return;

    const long double advance =
        static_cast<long double>(elapsed_us) * session->status.playback_rate;
    const long double candidate =
        static_cast<long double>(session->playback_anchor_position_us) + advance;
    const long double max_position =
        static_cast<long double>(std::numeric_limits<int64_t>::max());

    int64_t position_us = candidate >= max_position
                              ? std::numeric_limits<int64_t>::max()
                              : static_cast<int64_t>(candidate);

    if (session->status.duration_us > 0 && position_us >= session->status.duration_us) {
        position_us = session->status.duration_us;
        session->status.position_us = position_us;
        session->status.playback_state = DIGITOR_PLAYBACK_PAUSED;
        session->playback_anchor_valid = false;
#if defined(_WIN32)
        if (session->preview_audio) session->preview_audio->pause();
#endif
        return;
    }

    session->status.position_us = position_us;
}

void materialize_and_reanchor_locked(
    DigitorTimelineAudioSession* session,
    PlaybackClock::time_point now) noexcept {
    materialize_playback_position_locked(session, now);
    if (session->status.playback_state == DIGITOR_PLAYBACK_PLAYING) {
        anchor_playback_locked(session, now);
    }
}

} // namespace

extern "C" {

DigitorResult digitor_timeline_session_create(
    const DigitorTimelineSessionConfig* config,
    DigitorTimelineAudioSession** out_session) {
    if (out_session) *out_session = nullptr;
    if (!config || !out_session || !valid_config(*config)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        auto* session = new DigitorTimelineAudioSession();
        session->status.duration_us = config->duration_us;
        session->status.sample_rate = config->sample_rate;
        session->status.channels = config->channels;
        session->status.playback_state = DIGITOR_PLAYBACK_STOPPED;
        session->status.playback_rate = 1.0;
        session->status.preserve_pitch = 1;
        session->status.enable_dynamics = 1;
        *out_session = session;
        return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_timeline_session_destroy(DigitorTimelineAudioSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        delete session;
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_timeline_session_publish(
    DigitorTimelineAudioSession* session,
    const DigitorTimelinePublication* publication) {
    if (!session || !publication || publication->revision == 0 || publication->duration_us < 0)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        if (publication->revision <= session->status.revision) {
            ++session->telemetry.rejected_publications;
            return DIGITOR_RESULT_RESOURCE_IN_USE;
        }
#if defined(_WIN32)
        if (session->audio_pipeline) {
            std::string diagnostic;
            const auto audio_result = session->audio_pipeline->publish_single_source_snapshot(
                audio_revision_locked(session), publication->duration_us,
                session->status.master_gain_db,
                session->status.enable_dynamics != 0, &diagnostic);
            if (audio_result != DIGITOR_RESULT_OK) return audio_result;
        }
#endif
        const auto now = PlaybackClock::now();
        materialize_and_reanchor_locked(session, now);
        session->status.revision = publication->revision;
        session->status.duration_us = publication->duration_us;
        if (publication->duration_us > 0 &&
            session->status.position_us > publication->duration_us) {
            session->status.position_us = publication->duration_us;
            if (session->status.playback_state == DIGITOR_PLAYBACK_PLAYING) {
                session->status.playback_state = DIGITOR_PLAYBACK_PAUSED;
                session->playback_anchor_valid = false;
#if defined(_WIN32)
                if (session->preview_audio) session->preview_audio->pause();
#endif
            }
        } else if (session->status.playback_state == DIGITOR_PLAYBACK_PLAYING) {
            anchor_playback_locked(session, now);
        }
        ++session->telemetry.publications;
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_timeline_session_attach_media(
    DigitorTimelineAudioSession* session,
    const char* utf8_media_path) {
    if (!session || !utf8_media_path || !utf8_media_path[0])
        return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
#if defined(_WIN32)
        session->preview_audio.reset();
        session->audio_pipeline.reset();
        const auto acquired =
            digitor::acquire_production_audio_media_pipeline(utf8_media_path);
        if (acquired.no_audio_stream && acquired.result == DIGITOR_RESULT_OK)
            return DIGITOR_RESULT_OK;
        if (!acquired) return acquired.result;
        std::string diagnostic;
        const auto publish_result = acquired.pipeline->publish_single_source_snapshot(
            audio_revision_locked(session), session->status.duration_us,
            session->status.master_gain_db,
            session->status.enable_dynamics != 0, &diagnostic);
        if (publish_result != DIGITOR_RESULT_OK) return publish_result;

        auto decoder = std::make_unique<PipelinePlaybackDecoder>(acquired.pipeline);
        auto output = std::make_unique<WindowsPreviewAudioOutput>(std::move(decoder));
        auto result = output->prime(
            session->status.position_us, session->status.playback_rate);
        if (result != DIGITOR_RESULT_OK) return result;
        if (session->status.playback_state == DIGITOR_PLAYBACK_PLAYING) {
            result = output->play();
            if (result != DIGITOR_RESULT_OK) return result;
        }
        session->audio_pipeline = acquired.pipeline;
        session->preview_audio = std::move(output);
#else
        (void)utf8_media_path;
#endif
        return DIGITOR_RESULT_OK;
    } catch (const std::bad_alloc&) {
        return DIGITOR_RESULT_OUT_OF_MEMORY;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_timeline_session_detach_media(DigitorTimelineAudioSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
#if defined(_WIN32)
        session->preview_audio.reset();
        session->audio_pipeline.reset();
#endif
        return DIGITOR_RESULT_OK;
    } catch (...) {
        return DIGITOR_RESULT_INTERNAL_ERROR;
    }
}

DigitorResult digitor_timeline_session_play(DigitorTimelineAudioSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        if (session->status.revision == 0) return DIGITOR_RESULT_NOT_INITIALIZED;
#if defined(_WIN32)
        if (session->preview_audio) {
            const auto audio_result = session->preview_audio->play();
            if (audio_result != DIGITOR_RESULT_OK) return audio_result;
        }
#endif
        if (session->status.playback_state != DIGITOR_PLAYBACK_PLAYING) {
            session->status.playback_state = DIGITOR_PLAYBACK_PLAYING;
            anchor_playback_locked(session, PlaybackClock::now());
        }
        ++session->telemetry.play_commands;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_pause(DigitorTimelineAudioSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        materialize_playback_position_locked(session, PlaybackClock::now());
#if defined(_WIN32)
        if (session->preview_audio) {
            const auto audio_result = session->preview_audio->pause();
            if (audio_result != DIGITOR_RESULT_OK) return audio_result;
        }
#endif
        session->status.playback_state = DIGITOR_PLAYBACK_PAUSED;
        session->playback_anchor_valid = false;
        ++session->telemetry.pause_commands;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_stop(DigitorTimelineAudioSession* session) {
    if (!session) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
#if defined(_WIN32)
        if (session->preview_audio) {
            const auto audio_result = session->preview_audio->stop();
            if (audio_result != DIGITOR_RESULT_OK) return audio_result;
        }
#endif
        session->status.playback_state = DIGITOR_PLAYBACK_STOPPED;
        session->status.position_us = 0;
        session->playback_anchor_valid = false;
        ++session->status.seek_epoch;
        ++session->telemetry.stop_commands;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_seek(
    DigitorTimelineAudioSession* session,
    int64_t position_us) {
    if (!session || position_us < 0) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        if (session->status.duration_us > 0 &&
            position_us > session->status.duration_us)
            return DIGITOR_RESULT_INVALID_ARGUMENT;
#if defined(_WIN32)
        if (session->preview_audio) {
            const auto audio_result = session->preview_audio->seek(position_us);
            if (audio_result != DIGITOR_RESULT_OK) return audio_result;
        }
#endif
        session->status.position_us = position_us;
        if (session->status.playback_state == DIGITOR_PLAYBACK_PLAYING) {
            anchor_playback_locked(session, PlaybackClock::now());
        } else {
            session->playback_anchor_valid = false;
        }
        ++session->status.seek_epoch;
        ++session->telemetry.seek_commands;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_set_audio_controls(
    DigitorTimelineAudioSession* session,
    const DigitorAudioSessionControls* controls) {
    if (!session || !controls || !valid_controls(*controls)) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        const auto now = PlaybackClock::now();
        materialize_and_reanchor_locked(session, now);
#if defined(_WIN32)
        const auto previous_rate = session->status.playback_rate;
        if (session->preview_audio) {
            const auto audio_result = session->preview_audio->set_playback_rate(
                controls->playback_rate);
            if (audio_result != DIGITOR_RESULT_OK) return audio_result;
        }
        if (session->audio_pipeline) {
            const auto current_revision = audio_revision_locked(session);
            if (current_revision == (std::numeric_limits<std::uint64_t>::max)()) {
                if (session->preview_audio)
                    (void)session->preview_audio->set_playback_rate(previous_rate);
                return DIGITOR_RESULT_INVALID_ARGUMENT;
            }
            std::string diagnostic;
            const auto audio_result = session->audio_pipeline->publish_single_source_snapshot(
                current_revision + 1, session->status.duration_us,
                controls->master_gain_db, controls->enable_dynamics != 0,
                &diagnostic);
            if (audio_result != DIGITOR_RESULT_OK) {
                if (session->preview_audio)
                    (void)session->preview_audio->set_playback_rate(previous_rate);
                return audio_result;
            }
        }
#endif
        session->status.master_gain_db = controls->master_gain_db;
        session->status.playback_rate = controls->playback_rate;
        session->status.preserve_pitch = controls->preserve_pitch ? 1 : 0;
        session->status.enable_dynamics = controls->enable_dynamics ? 1 : 0;
        if (session->status.playback_state == DIGITOR_PLAYBACK_PLAYING) {
            anchor_playback_locked(session, now);
        }
        ++session->telemetry.control_updates;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_get_status(
    DigitorTimelineAudioSession* session,
    DigitorTimelineSessionStatus* out_status) {
    if (out_status) *out_status = {};
    if (!session || !out_status) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        materialize_playback_position_locked(session, PlaybackClock::now());
        *out_status = session->status;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

DigitorResult digitor_timeline_session_get_telemetry(
    DigitorTimelineAudioSession* session,
    DigitorTimelineSessionTelemetry* out_telemetry) {
    if (out_telemetry) *out_telemetry = {};
    if (!session || !out_telemetry) return DIGITOR_RESULT_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(session->mutex);
        *out_telemetry = session->telemetry;
        return DIGITOR_RESULT_OK;
    } catch (...) { return DIGITOR_RESULT_INTERNAL_ERROR; }
}

} // extern "C"
