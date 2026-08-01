#include "digitor/production_playback_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace digitor {
namespace {
using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point start) noexcept {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void update_average(double& average, std::uint64_t count, double sample) noexcept {
  if (count == 0) average = sample;
  else average += (sample - average) / static_cast<double>(count + 1);
}
}  // namespace

ProductionPlaybackEngine::ProductionPlaybackEngine(ProductionPlaybackConfig config,
                                                   PlaybackDecodeCallback decode,
                                                   PlaybackPresentCallback present)
    : config_(config), decode_(std::move(decode)), present_(std::move(present)),
      transport_(config.duration_us), loop_out_us_(config.duration_us) {
  if (!decode_ || !present_) throw std::invalid_argument("playback decode and present callbacks are required");
  if (!std::isfinite(config_.target_fps) || config_.target_fps <= 0.0 || config_.target_fps > 240.0)
    throw std::invalid_argument("target playback frame rate is invalid");
  if (config_.prefetch_frames == 0 || config_.maximum_queued_frames == 0 ||
      config_.prefetch_frames > config_.maximum_queued_frames)
    throw std::invalid_argument("playback queue limits are invalid");
  if (config_.gpu_memory_budget_bytes == 0 || config_.late_frame_threshold_us < 0)
    throw std::invalid_argument("playback memory or lateness policy is invalid");
  worker_ = std::thread([this] { worker_loop(); });
}

ProductionPlaybackEngine::~ProductionPlaybackEngine() {
  {
    std::lock_guard lock(mutex_);
    shutdown_ = true;
  }
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}

std::int64_t ProductionPlaybackEngine::frame_interval_us() const noexcept {
  return std::max<std::int64_t>(1, static_cast<std::int64_t>(1000000.0 / config_.target_fps));
}

bool ProductionPlaybackEngine::frame_is_acceptable(const ProductionPlaybackFrame& frame) const noexcept {
  if (!frame.frame || frame.duration_us < 0 || frame.estimated_bytes == 0) return false;
  return !config_.require_gpu_frames || frame.frame->backend() != DIGITOR_RENDERER_CPU;
}

void ProductionPlaybackEngine::play(std::int64_t now_us) {
  transport_.play(now_us);
  {
    std::lock_guard lock(mutex_);
    playing_ = true;
    requested_position_us_ = transport_.position_us(now_us);
    if (queue_.empty()) next_decode_pts_us_ = requested_position_us_;
  }
  wake_.notify_all();
}

void ProductionPlaybackEngine::pause(std::int64_t now_us) {
  transport_.pause(now_us);
  std::lock_guard lock(mutex_);
  playing_ = false;
  requested_position_us_ = transport_.position_us(now_us);
}

void ProductionPlaybackEngine::stop() {
  transport_.stop();
  {
    std::lock_guard lock(mutex_);
    playing_ = false;
    ++generation_;
    queue_.clear();
    queued_bytes_ = 0;
    requested_position_us_ = 0;
    next_decode_pts_us_ = 0;
    counters_.last_presented_pts_us = -1;
  }
  wake_.notify_all();
}

void ProductionPlaybackEngine::flush_for_generation(std::uint64_t generation) {
  queue_.clear();
  queued_bytes_ = 0;
  generation_ = generation;
  counters_.last_presented_pts_us = -1;
}

void ProductionPlaybackEngine::seek(std::int64_t position_us, std::int64_t now_us) {
  transport_.seek(position_us, now_us);
  {
    std::lock_guard lock(mutex_);
    ++counters_.seek_requests;
    if (!queue_.empty()) ++counters_.coalesced_seeks;
    flush_for_generation(transport_.snapshot(now_us).seek_generation);
    requested_position_us_ = transport_.position_us(now_us);
    next_decode_pts_us_ = requested_position_us_;
  }
  wake_.notify_all();
}

void ProductionPlaybackEngine::scrub(std::int64_t position_us, std::int64_t now_us) {
  seek(position_us, now_us);
  {
    std::lock_guard lock(mutex_);
    playing_ = true;
  }
  wake_.notify_all();
}

void ProductionPlaybackEngine::step_frames(std::int64_t frame_count, std::int64_t now_us) {
  const auto current = transport_.position_us(now_us);
  scrub(current + frame_count * frame_interval_us(), now_us);
}

void ProductionPlaybackEngine::set_loop_range(std::int64_t in_us, std::int64_t out_us, bool enabled) {
  if (in_us < 0 || out_us <= in_us || out_us > config_.duration_us)
    throw std::invalid_argument("playback loop range is invalid");
  std::lock_guard lock(mutex_);
  loop_in_us_ = in_us;
  loop_out_us_ = out_us;
  loop_enabled_ = enabled;
}

bool ProductionPlaybackEngine::set_rate(double rate, std::int64_t now_us) {
  const auto accepted = transport_.set_rate(rate, now_us);
  if (!accepted) return false;
  {
    std::lock_guard lock(mutex_);
    rate_ = rate;
    requested_position_us_ = transport_.position_us(now_us);
    next_decode_pts_us_ = requested_position_us_;
    queue_.clear();
    queued_bytes_ = 0;
  }
  wake_.notify_all();
  return true;
}

std::int64_t ProductionPlaybackEngine::update_audio_clock(std::int64_t raw_audio_clock_us,
                                                          std::int64_t now_us) {
  return transport_.update_audio_clock(raw_audio_clock_us, now_us);
}

void ProductionPlaybackEngine::notify_audio_device_changed() { transport_.notify_audio_device_changed(); }
bool ProductionPlaybackEngine::refresh_audio_device(std::int64_t now_us) {
  return transport_.refresh_audio_device(now_us);
}

void ProductionPlaybackEngine::set_memory_pressure(PlaybackPressure pressure) {
  {
    std::lock_guard lock(mutex_);
    memory_pressure_ = pressure;
    update_quality_locked();
  }
  wake_.notify_all();
}

void ProductionPlaybackEngine::set_thermal_pressure(PlaybackPressure pressure) {
  {
    std::lock_guard lock(mutex_);
    thermal_pressure_ = pressure;
    update_quality_locked();
  }
  wake_.notify_all();
}

void ProductionPlaybackEngine::set_proxy_available(bool available) noexcept {
  std::lock_guard lock(mutex_);
  proxy_available_ = available;
  update_quality_locked();
}

void ProductionPlaybackEngine::update_quality_locked() {
  if (!config_.adaptive_quality) {
    quality_ = PlaybackQuality::full;
    return;
  }
  const auto pressure = std::max(memory_pressure_, thermal_pressure_);
  if (pressure == PlaybackPressure::critical)
    quality_ = proxy_available_ ? PlaybackQuality::proxy : PlaybackQuality::quarter;
  else if (pressure == PlaybackPressure::elevated)
    quality_ = PlaybackQuality::half;
  else if (counters_.dropped_frames > counters_.presented_frames / 10 + 2)
    quality_ = proxy_available_ ? PlaybackQuality::proxy : PlaybackQuality::quarter;
  else if (counters_.dropped_frames > 0)
    quality_ = PlaybackQuality::half;
  else
    quality_ = PlaybackQuality::full;
}

void ProductionPlaybackEngine::worker_loop() {
  for (;;) {
    PlaybackQuality quality;
    std::uint64_t generation;
    std::int64_t decode_pts;
    double rate;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] {
        return shutdown_ || (playing_ && queue_.size() < config_.prefetch_frames &&
                             queued_bytes_ < config_.gpu_memory_budget_bytes);
      });
      if (shutdown_) return;
      quality = quality_;
      generation = generation_;
      rate = rate_;
      decode_pts = rate < 0.0 ? std::min(next_decode_pts_us_, requested_position_us_)
                              : std::max(next_decode_pts_us_, requested_position_us_);
      decode_pts = std::clamp<std::int64_t>(decode_pts, 0, config_.duration_us);
      const auto direction = rate < 0.0 ? -1 : 1;
      next_decode_pts_us_ = decode_pts + direction * frame_interval_us();
      if (loop_enabled_) {
        if (direction > 0 && next_decode_pts_us_ >= loop_out_us_) next_decode_pts_us_ = loop_in_us_;
        if (direction < 0 && next_decode_pts_us_ <= loop_in_us_) next_decode_pts_us_ = loop_out_us_;
      }
    }

    const auto start = Clock::now();
    std::optional<ProductionPlaybackFrame> decoded;
    try {
      decoded = decode_(decode_pts, quality, generation);
    } catch (const std::exception& e) {
      std::lock_guard lock(mutex_);
      ++counters_.decode_failures;
      counters_.last_error = e.what();
      continue;
    } catch (...) {
      std::lock_guard lock(mutex_);
      ++counters_.decode_failures;
      counters_.last_error = "unexpected playback decode failure";
      continue;
    }
    const auto decode_ms = elapsed_ms(start);

    std::lock_guard lock(mutex_);
    if (!decoded) {
      ++counters_.decode_failures;
      counters_.last_error = "decoder returned no playback frame";
      continue;
    }
    decoded->seek_generation = generation;
    decoded->quality = quality;
    if (!frame_is_acceptable(*decoded)) {
      ++counters_.decode_failures;
      if (decoded->frame && decoded->frame->backend() == DIGITOR_RENDERER_CPU)
        ++counters_.cpu_frame_rejections;
      counters_.last_error = "playback decoder returned an invalid or CPU-resident frame";
      continue;
    }
    if (generation != generation_) {
      ++counters_.stale_frames;
      continue;
    }
    while (!queue_.empty() &&
           (queue_.size() >= config_.maximum_queued_frames ||
            queued_bytes_ + decoded->estimated_bytes > config_.gpu_memory_budget_bytes)) {
      queued_bytes_ -= queue_.front().estimated_bytes;
      queue_.pop_front();
      ++counters_.dropped_frames;
    }
    queued_bytes_ += decoded->estimated_bytes;
    queue_.push_back(std::move(*decoded));
    update_average(counters_.average_decode_ms, counters_.decoded_frames, decode_ms);
    ++counters_.decoded_frames;
    update_quality_locked();
  }
}

DigitorResult ProductionPlaybackEngine::tick(std::int64_t now_us) {
  auto position = transport_.position_us(now_us);
  bool loop = false;
  std::int64_t loop_target{};
  {
    std::lock_guard lock(mutex_);
    if (loop_enabled_ && rate_ >= 0.0 && position >= loop_out_us_) {
      loop = true;
      loop_target = loop_in_us_;
    } else if (loop_enabled_ && rate_ < 0.0 && position <= loop_in_us_) {
      loop = true;
      loop_target = loop_out_us_;
    }
  }
  if (loop) {
    seek(loop_target, now_us);
    transport_.play(now_us);
    {
      std::lock_guard lock(mutex_);
      ++counters_.loop_count;
      playing_ = true;
    }
    position = loop_target;
  }

  std::optional<ProductionPlaybackFrame> selected;
  {
    std::lock_guard lock(mutex_);
    requested_position_us_ = position;
    while (!queue_.empty() && queue_.front().seek_generation != generation_) {
      queued_bytes_ -= queue_.front().estimated_bytes;
      queue_.pop_front();
      ++counters_.stale_frames;
    }
    if (rate_ >= 0.0) {
      while (queue_.size() > 1 && queue_[1].pts_us <= position) {
        queued_bytes_ -= queue_.front().estimated_bytes;
        queue_.pop_front();
        ++counters_.dropped_frames;
      }
      if (!queue_.empty() && queue_.front().pts_us <= position + frame_interval_us()) {
        selected = std::move(queue_.front());
        queued_bytes_ -= queue_.front().estimated_bytes;
        queue_.pop_front();
      }
    } else {
      while (queue_.size() > 1 && queue_[1].pts_us >= position) {
        queued_bytes_ -= queue_.front().estimated_bytes;
        queue_.pop_front();
        ++counters_.dropped_frames;
      }
      if (!queue_.empty() && queue_.front().pts_us >= position - frame_interval_us()) {
        selected = std::move(queue_.front());
        queued_bytes_ -= queue_.front().estimated_bytes;
        queue_.pop_front();
      }
    }
    update_quality_locked();
  }
  wake_.notify_all();
  if (!selected) return DIGITOR_RESULT_RESOURCE_IN_USE;
  if (rate_ >= 0.0 && selected->pts_us + selected->duration_us + config_.late_frame_threshold_us < position) {
    std::lock_guard lock(mutex_);
    ++counters_.dropped_frames;
    return DIGITOR_RESULT_RESOURCE_IN_USE;
  }

  const auto start = Clock::now();
  const auto result = present_(*selected);
  const auto present_ms = elapsed_ms(start);
  {
    std::lock_guard lock(mutex_);
    if (result == DIGITOR_RESULT_OK) {
      update_average(counters_.average_present_ms, counters_.presented_frames, present_ms);
      ++counters_.presented_frames;
      counters_.last_presented_pts_us = selected->pts_us;
      counters_.last_error.clear();
    } else {
      ++counters_.presentation_failures;
      counters_.last_error = "playback presentation failed";
    }
    update_quality_locked();
  }
  return result;
}

ProductionPlaybackTelemetry ProductionPlaybackEngine::telemetry(std::int64_t now_us) const {
  std::lock_guard lock(mutex_);
  auto out = counters_;
  out.transport = transport_.snapshot(now_us);
  out.quality = quality_;
  out.memory_pressure = memory_pressure_;
  out.thermal_pressure = thermal_pressure_;
  out.queued_frames = queue_.size();
  out.queued_bytes = queued_bytes_;
  return out;
}

}  // namespace digitor
