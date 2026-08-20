#pragma once

#if defined(__ANDROID__)

#include "digitor/android_hardware_encode_adapter.hpp"

#include <fcntl.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace digitor::android_source_audio_mux_detail {

struct Extractor final {
  AMediaExtractor* value{};
  ~Extractor() {
    if (value) AMediaExtractor_delete(value);
  }
};

inline DigitorResult open_extractor(const std::string& path, Extractor& out,
                                    std::string& diagnostic) noexcept {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    diagnostic = "Android MP4 remux could not open media source";
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }
  struct stat st {};
  if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
    ::close(fd);
    diagnostic = "Android MP4 remux source is unreadable";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  out.value = AMediaExtractor_new();
  if (!out.value) {
    ::close(fd);
    diagnostic = "failed to allocate Android MediaExtractor";
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  }
  const auto status = AMediaExtractor_setDataSourceFd(
      out.value, fd, 0, static_cast<off64_t>(st.st_size));
  ::close(fd);
  if (status != AMEDIA_OK) {
    diagnostic = "Android MediaExtractor could not open media source";
    return DIGITOR_RESULT_NOT_INITIALIZED;
  }
  return DIGITOR_RESULT_OK;
}

struct Track final {
  std::size_t source_index{};
  AMediaFormat* format{};
  std::string mime;
  std::size_t buffer_capacity{};
  ~Track() {
    if (format) AMediaFormat_delete(format);
  }
};

inline bool find_track(AMediaExtractor* extractor, const char* prefix,
                       Track& out) noexcept {
  if (!extractor || !prefix) return false;
  const auto count = AMediaExtractor_getTrackCount(extractor);
  for (std::size_t index = 0; index < count; ++index) {
    AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor, index);
    if (!format) continue;
    const char* mime = nullptr;
    const bool has_mime =
        AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime) && mime;
    if (!has_mime || std::strncmp(mime, prefix, std::strlen(prefix)) != 0) {
      AMediaFormat_delete(format);
      continue;
    }
    std::int32_t max_input_size = 0;
    (void)AMediaFormat_getInt32(format, "max-input-size", &max_input_size);
    constexpr std::size_t kDefaultCapacity = 8u * 1024u * 1024u;
    constexpr std::size_t kMaximumCapacity = 32u * 1024u * 1024u;
    std::size_t capacity = kDefaultCapacity;
    if (max_input_size > 0) {
      capacity = static_cast<std::size_t>(max_input_size);
      capacity = (std::max)(capacity, std::size_t{256u * 1024u});
      capacity = (std::min)(capacity, kMaximumCapacity);
    }
    out.source_index = index;
    out.format = format;
    out.mime = mime;
    out.buffer_capacity = capacity;
    return true;
  }
  return false;
}

inline DigitorResult copy_track(AMediaExtractor* extractor,
                                std::size_t source_track,
                                AMediaMuxer* muxer,
                                std::size_t destination_track,
                                std::int64_t source_start_us,
                                std::int64_t duration_us,
                                std::size_t buffer_capacity,
                                std::uint64_t& samples,
                                std::string& diagnostic) noexcept {
  samples = 0;
  if (!extractor || !muxer || duration_us <= 0 || !buffer_capacity) {
    diagnostic = "Android MP4 remux track arguments are invalid";
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  }
  if (AMediaExtractor_selectTrack(extractor, source_track) != AMEDIA_OK) {
    diagnostic = "Android MediaExtractor could not select remux track";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (source_start_us > 0) {
    AMediaExtractor_seekTo(extractor, source_start_us,
                           AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
  }

  std::vector<std::uint8_t> buffer;
  try {
    buffer.resize(buffer_capacity);
  } catch (...) {
    diagnostic = "out of memory allocating Android remux sample buffer";
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  }

  for (;;) {
    const auto source_pts = AMediaExtractor_getSampleTime(extractor);
    if (source_pts < 0) break;
    if (source_pts < source_start_us) {
      if (!AMediaExtractor_advance(extractor)) break;
      continue;
    }
    const auto output_pts = source_pts - source_start_us;
    if (output_pts < 0) {
      if (!AMediaExtractor_advance(extractor)) break;
      continue;
    }
    if (output_pts >= duration_us) break;

    const auto bytes = AMediaExtractor_readSampleData(
        extractor, buffer.data(), buffer.size());
    if (bytes < 0) break;
    if (bytes == 0) {
      if (!AMediaExtractor_advance(extractor)) break;
      continue;
    }
    if (static_cast<std::size_t>(bytes) > buffer.size() ||
        bytes > (std::numeric_limits<std::int32_t>::max)()) {
      diagnostic = "Android MP4 remux sample exceeds bounded buffer";
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    AMediaCodecBufferInfo info{};
    info.offset = 0;
    info.size = static_cast<std::int32_t>(bytes);
    info.presentationTimeUs = output_pts;
    const auto sample_flags = AMediaExtractor_getSampleFlags(extractor);
    info.flags = (sample_flags & AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC)
                     ? AMEDIACODEC_BUFFER_FLAG_KEY_FRAME
                     : 0;
    if (AMediaMuxer_writeSampleData(muxer, destination_track, buffer.data(),
                                    &info) != AMEDIA_OK) {
      diagnostic = "Android MP4 muxer failed to write compressed sample";
      return DIGITOR_RESULT_INTERNAL_ERROR;
    }
    ++samples;
    if (!AMediaExtractor_advance(extractor)) break;
  }
  diagnostic.clear();
  return DIGITOR_RESULT_OK;
}

struct AudioMuxState final {
  AndroidHardwareEncoderHost inner;
  std::shared_ptr<const ExportRenderSnapshot> snapshot;
  mutable std::mutex mutex;
  std::string final_path;
  std::string video_path;
  std::string remux_path;
  std::int64_t duration_us{};
  bool opened{};
  bool cancelled{};
  bool final_published{};
  bool source_audio_present{};
  bool audio_track_muxed{};
  std::uint64_t audio_samples_muxed{};
};

inline DigitorResult probe_source_audio(AudioMuxState& state,
                                        std::string& diagnostic) noexcept {
  Extractor source;
  const auto result = open_extractor(state.snapshot->data().source_media_path,
                                     source, diagnostic);
  if (result != DIGITOR_RESULT_OK) return result;
  Track audio;
  if (!find_track(source.value, "audio/", audio)) {
    state.source_audio_present = false;
    diagnostic.clear();
    return DIGITOR_RESULT_OK;
  }
  state.source_audio_present = true;
  if (audio.mime != "audio/mp4a-latm") {
    diagnostic =
        "Android MP4 export currently preserves AAC source audio only";
    return DIGITOR_RESULT_UNSUPPORTED;
  }
  diagnostic.clear();
  return DIGITOR_RESULT_OK;
}

inline DigitorResult publish_video_only(AudioMuxState& state,
                                        std::string& diagnostic) noexcept {
  if (std::rename(state.video_path.c_str(), state.final_path.c_str()) != 0) {
    diagnostic = "Android export could not atomically publish video-only MP4";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  state.final_published = true;
  diagnostic.clear();
  return DIGITOR_RESULT_OK;
}

inline DigitorResult remux_source_audio(AudioMuxState& state,
                                        std::string& diagnostic) noexcept {
  Extractor video;
  Extractor source;
  auto result = open_extractor(state.video_path, video, diagnostic);
  if (result != DIGITOR_RESULT_OK) return result;
  result = open_extractor(state.snapshot->data().source_media_path, source,
                          diagnostic);
  if (result != DIGITOR_RESULT_OK) return result;

  Track video_track;
  Track audio_track;
  if (!find_track(video.value, "video/", video_track)) {
    diagnostic = "Android remux source contains no encoded video track";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (!find_track(source.value, "audio/", audio_track)) {
    diagnostic = "source audio disappeared before Android MP4 remux";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  if (audio_track.mime != "audio/mp4a-latm") {
    diagnostic = "Android MP4 remux requires AAC source audio";
    return DIGITOR_RESULT_UNSUPPORTED;
  }

  (void)::unlink(state.remux_path.c_str());
  const int output_fd = ::open(state.remux_path.c_str(),
                               O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
  if (output_fd < 0) {
    diagnostic = "Android audio remux destination is not writable";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  AMediaMuxer* muxer =
      AMediaMuxer_new(output_fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
  if (!muxer) {
    ::close(output_fd);
    diagnostic = "failed to allocate Android MP4 audio remuxer";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  const auto video_out = AMediaMuxer_addTrack(muxer, video_track.format);
  const auto audio_out = AMediaMuxer_addTrack(muxer, audio_track.format);
  if (video_out < 0 || audio_out < 0 || AMediaMuxer_start(muxer) != AMEDIA_OK) {
    AMediaMuxer_delete(muxer);
    ::close(output_fd);
    (void)::unlink(state.remux_path.c_str());
    diagnostic = "Android MP4 muxer rejected video/audio tracks";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

  std::uint64_t video_samples = 0;
  std::uint64_t audio_samples = 0;
  result = copy_track(video.value, video_track.source_index, muxer,
                      static_cast<std::size_t>(video_out), 0,
                      state.duration_us, video_track.buffer_capacity,
                      video_samples, diagnostic);
  if (result == DIGITOR_RESULT_OK) {
    result = copy_track(source.value, audio_track.source_index, muxer,
                        static_cast<std::size_t>(audio_out),
                        state.snapshot->data().source_start_us,
                        state.duration_us, audio_track.buffer_capacity,
                        audio_samples, diagnostic);
  }

  const auto stop = AMediaMuxer_stop(muxer);
  AMediaMuxer_delete(muxer);
  ::close(output_fd);
  if (result != DIGITOR_RESULT_OK || stop != AMEDIA_OK || video_samples == 0 ||
      audio_samples == 0) {
    (void)::unlink(state.remux_path.c_str());
    if (result == DIGITOR_RESULT_OK) {
      diagnostic = video_samples == 0
                       ? "Android MP4 remux produced no video samples"
                       : audio_samples == 0
                             ? "source AAC track produced no samples in export range"
                             : "Android MP4 remux could not finalize";
      result = DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    return result;
  }

  if (std::rename(state.remux_path.c_str(), state.final_path.c_str()) != 0) {
    (void)::unlink(state.remux_path.c_str());
    diagnostic = "Android export could not atomically publish audio MP4";
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  (void)::unlink(state.video_path.c_str());
  state.audio_samples_muxed = audio_samples;
  state.audio_track_muxed = true;
  state.final_published = true;
  diagnostic.clear();
  return DIGITOR_RESULT_OK;
}

inline AndroidHardwareEncoderHost wrap_source_audio_mux(
    AndroidHardwareEncoderHost inner,
    std::shared_ptr<const ExportRenderSnapshot> snapshot) {
  auto state = std::make_shared<AudioMuxState>();
  state->inner = std::move(inner);
  state->snapshot = std::move(snapshot);

  AndroidHardwareEncoderHost host{};
  host.open = [state](const HardwareEncodeConfig& config,
                      const ExportRenderSnapshot& snapshot_ref,
                      AndroidHardwareEncodeCapabilities& capabilities,
                      std::string& diagnostic) {
    std::scoped_lock lock(state->mutex);
    if (!state->snapshot || state->opened) {
      diagnostic = "Android source-audio mux host is already open or unfrozen";
      return state->opened ? DIGITOR_RESULT_RESOURCE_IN_USE
                           : DIGITOR_RESULT_INVALID_ARGUMENT;
    }
    state->final_path = config.output_path;
    state->video_path = config.output_path + ".digitor-video-only";
    state->remux_path = config.output_path + ".digitor-av-partial";
    state->duration_us = config.duration_us;
    state->source_audio_present = false;
    state->audio_track_muxed = false;
    state->audio_samples_muxed = 0;
    state->final_published = false;
    (void)::unlink(state->video_path.c_str());
    (void)::unlink(state->remux_path.c_str());

    auto result = probe_source_audio(*state, diagnostic);
    if (result != DIGITOR_RESULT_OK) return result;
    auto inner_config = config;
    inner_config.output_path = state->video_path;
    result = state->inner.open(inner_config, snapshot_ref, capabilities,
                               diagnostic);
    if (result == DIGITOR_RESULT_OK) state->opened = true;
    return result;
  };
  host.submit = [state](const AndroidHardwareEncodeFrameDescriptor& frame,
                        std::string& diagnostic) {
    std::scoped_lock lock(state->mutex);
    return state->opened && !state->cancelled
               ? state->inner.submit(frame, diagnostic)
               : DIGITOR_RESULT_NOT_INITIALIZED;
  };
  host.drain = [state](std::string& diagnostic) {
    std::scoped_lock lock(state->mutex);
    return state->opened && !state->cancelled
               ? state->inner.drain(diagnostic)
               : DIGITOR_RESULT_NOT_INITIALIZED;
  };
  host.finalize_mp4_atomic = [state](std::string& diagnostic) {
    std::scoped_lock lock(state->mutex);
    if (!state->opened || state->cancelled)
      return DIGITOR_RESULT_NOT_INITIALIZED;
    const auto inner_result = state->inner.finalize_mp4_atomic(diagnostic);
    if (inner_result != DIGITOR_RESULT_OK) return inner_result;
    const auto result = state->source_audio_present
                            ? remux_source_audio(*state, diagnostic)
                            : publish_video_only(*state, diagnostic);
    if (result != DIGITOR_RESULT_OK) {
      (void)::unlink(state->video_path.c_str());
      (void)::unlink(state->remux_path.c_str());
      return result;
    }
    return DIGITOR_RESULT_OK;
  };
  host.cancel = [state]() {
    std::scoped_lock lock(state->mutex);
    if (state->inner.cancel) state->inner.cancel();
    state->cancelled = true;
    if (!state->video_path.empty()) (void)::unlink(state->video_path.c_str());
    if (!state->remux_path.empty()) (void)::unlink(state->remux_path.c_str());
  };
  host.qualification = [state]() {
    std::scoped_lock lock(state->mutex);
    auto q = state->inner.qualification();
    q.source_audio_present = state->source_audio_present;
    q.audio_track_muxed = state->audio_track_muxed;
    q.audio_samples_muxed = state->audio_samples_muxed;
    q.mp4_finalized = q.mp4_finalized && state->final_published;
    return q;
  };
  return host;
}

}  // namespace digitor::android_source_audio_mux_detail

#endif  // defined(__ANDROID__)
