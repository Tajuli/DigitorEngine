#include "digitor/unified_real_media_runtime.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace digitor {
namespace {
std::uint64_t bytes_per_pixel(DigitorPixelFormat format) noexcept {
  switch (format) {
    case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT: return 8;
    case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT: return 16;
    default: return 4;
  }
}
}  // namespace

UnifiedRealMediaRuntime::UnifiedRealMediaRuntime(
    std::unique_ptr<ProductionHardwareDecodeSession> decode,
    UnifiedRealMediaRuntimeConfig config,
    TimelineFrameResolver resolver,
    NativeFlutterPresenter presenter,
    ExistingGpuPipeline pipeline)
    : decode_(std::move(decode)), resolver_(std::move(resolver)),
      pipeline_(std::move(pipeline)), presenter_(std::move(presenter)),
      default_estimated_frame_bytes_(config.default_estimated_frame_bytes) {
  if (!decode_) throw std::invalid_argument("production hardware decode session is required");
  if (!resolver_) throw std::invalid_argument("timeline frame resolver is required");
  if (!presenter_) throw std::invalid_argument("native Flutter presenter is required");

  playback_ = std::make_unique<ProductionPlaybackEngine>(
      config.playback,
      [this](std::int64_t pts, PlaybackQuality quality, std::uint64_t generation) {
        return decode_for_playback(pts, quality, generation);
      },
      [this](const ProductionPlaybackFrame& frame) {
        return present_for_flutter(frame);
      });
}

UnifiedRealMediaRuntime::~UnifiedRealMediaRuntime() {
  if (playback_) playback_->stop();
  std::scoped_lock lock(mutex_);
  retained_surfaces_.clear();
  last_texture_.reset();
}

std::uint64_t UnifiedRealMediaRuntime::estimate_bytes(
    const GpuFrameMetadata& metadata, std::uint64_t fallback) noexcept {
  if (metadata.width == 0 || metadata.height == 0) return fallback;
  const auto bpp = bytes_per_pixel(metadata.format);
  if (metadata.width > std::numeric_limits<std::uint64_t>::max() / metadata.height / bpp)
    return fallback;
  return static_cast<std::uint64_t>(metadata.width) * metadata.height * bpp;
}

std::optional<ProductionPlaybackFrame> UnifiedRealMediaRuntime::decode_for_playback(
    std::int64_t pts_us, PlaybackQuality quality, std::uint64_t generation) {
  const auto frame_number = resolver_(pts_us, quality);
  if (!frame_number) return std::nullopt;

  ProductionDecodedFrame decoded;
  std::string diagnostic;
  const auto decode_result = decode_->decode(*frame_number, decoded, &diagnostic);
  if (decode_result != DIGITOR_RESULT_OK || !decoded.gpu_frame)
    throw std::runtime_error(diagnostic.empty() ? "production hardware decode failed" : diagnostic);

  ProcessedGpuFramePtr processed = decoded.gpu_frame;
  if (pipeline_) {
    processed.reset();
    const auto pipeline_result = pipeline_(decoded, quality, processed);
    if (pipeline_result != DIGITOR_RESULT_OK || !processed)
      throw std::runtime_error("existing GPU timeline/node/color pipeline rejected decoded frame");
  }
  if (processed->backend() == DIGITOR_RENDERER_CPU)
    throw std::runtime_error("unified real-media runtime refuses CPU-resident playback frames");

  const auto identity = processed->identity();
  {
    std::scoped_lock lock(mutex_);
    retained_surfaces_[identity] = decoded.decoder_surface;
  }

  ProductionPlaybackFrame output;
  output.frame = std::move(processed);
  output.pts_us = decoded.pts;
  output.duration_us = decoded.duration;
  output.seek_generation = generation;
  output.estimated_bytes = estimate_bytes(output.frame->metadata(), default_estimated_frame_bytes_);
  output.quality = quality;
  if (output.estimated_bytes == 0) {
    std::scoped_lock lock(mutex_);
    retained_surfaces_.erase(identity);
    throw std::runtime_error("cannot determine GPU frame memory cost");
  }
  return output;
}

DigitorResult UnifiedRealMediaRuntime::present_for_flutter(
    const ProductionPlaybackFrame& frame) noexcept {
  if (!frame.frame || frame.frame->backend() == DIGITOR_RENDERER_CPU)
    return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

  UnifiedNativeTextureDescriptor descriptor;
  const auto& metadata = frame.frame->metadata();
  descriptor.backend = frame.frame->backend();
  descriptor.format = metadata.format;
  descriptor.width = metadata.width;
  descriptor.height = metadata.height;
  descriptor.timestamp_us = frame.pts_us;
  descriptor.frame_identity = frame.frame->identity();
  {
    std::scoped_lock lock(mutex_);
    descriptor.generation = ++presentation_generation_;
  }

  DigitorResult result = DIGITOR_RESULT_INTERNAL_ERROR;
  try {
    result = presenter_(frame.frame, descriptor);
  } catch (const std::bad_alloc&) {
    result = DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    result = DIGITOR_RESULT_INTERNAL_ERROR;
  }

  std::scoped_lock lock(mutex_);
  if (result == DIGITOR_RESULT_OK) last_texture_ = descriptor;
  retained_surfaces_.erase(frame.frame->identity());
  return result;
}

void UnifiedRealMediaRuntime::play(std::int64_t now) { playback_->play(now); }
void UnifiedRealMediaRuntime::pause(std::int64_t now) { playback_->pause(now); }
void UnifiedRealMediaRuntime::stop() {
  playback_->stop();
  std::scoped_lock lock(mutex_);
  retained_surfaces_.clear();
}

DigitorResult UnifiedRealMediaRuntime::seek(std::int64_t pts, std::int64_t now,
                                            std::string* diagnostic) noexcept {
  const auto result = decode_->seek(pts, diagnostic);
  if (result != DIGITOR_RESULT_OK) return result;
  {
    std::scoped_lock lock(mutex_);
    retained_surfaces_.clear();
  }
  playback_->seek(pts, now);
  return DIGITOR_RESULT_OK;
}

DigitorResult UnifiedRealMediaRuntime::scrub(std::int64_t pts, std::int64_t now,
                                             std::string* diagnostic) noexcept {
  const auto result = decode_->seek(pts, diagnostic);
  if (result != DIGITOR_RESULT_OK) return result;
  {
    std::scoped_lock lock(mutex_);
    retained_surfaces_.clear();
  }
  playback_->scrub(pts, now);
  return DIGITOR_RESULT_OK;
}

void UnifiedRealMediaRuntime::step_frames(std::int64_t count, std::int64_t now) {
  playback_->step_frames(count, now);
}
bool UnifiedRealMediaRuntime::set_rate(double rate, std::int64_t now) {
  return playback_->set_rate(rate, now);
}
DigitorResult UnifiedRealMediaRuntime::tick(std::int64_t now) { return playback_->tick(now); }
std::int64_t UnifiedRealMediaRuntime::update_audio_clock(std::int64_t audio, std::int64_t now) {
  return playback_->update_audio_clock(audio, now);
}
void UnifiedRealMediaRuntime::set_memory_pressure(PlaybackPressure pressure) {
  playback_->set_memory_pressure(pressure);
}
void UnifiedRealMediaRuntime::set_thermal_pressure(PlaybackPressure pressure) {
  playback_->set_thermal_pressure(pressure);
}
void UnifiedRealMediaRuntime::set_proxy_available(bool available) noexcept {
  playback_->set_proxy_available(available);
}
ProductionPlaybackTelemetry UnifiedRealMediaRuntime::telemetry(std::int64_t now) const {
  return playback_->telemetry(now);
}
HardwareDecodeQualification UnifiedRealMediaRuntime::decode_qualification() const {
  return decode_->qualification();
}
std::optional<UnifiedNativeTextureDescriptor> UnifiedRealMediaRuntime::last_native_texture() const {
  std::scoped_lock lock(mutex_);
  return last_texture_;
}

}  // namespace digitor
