#include "digitor/timeline_render_runtime.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace digitor {

bool RenderVideoFrame::valid() const noexcept {
  if (width == 0 || height == 0) return false;
  if (gpu) {
    const auto& metadata = gpu->metadata();
    return gpu->ready() && metadata.width == width && metadata.height == height;
  }
  const auto pixels = static_cast<std::size_t>(width) * height;
  return pixels <= std::numeric_limits<std::size_t>::max() / 4U &&
         rgba.size() == pixels * 4U;
}

TimelineRenderRuntime::TimelineRenderRuntime(TimelineRenderExecutor executor,
                                             TimelineRenderCallbacks callbacks,
                                             std::size_t memory_cache_bytes,
                                             std::size_t gpu_cache_frames)
    : executor_(std::move(executor)),
      callbacks_(std::move(callbacks)),
      cache_(memory_cache_bytes),
      gpu_cache_capacity_(gpu_cache_frames) {}

bool TimelineRenderRuntime::is_cancelled() const {
  return callbacks_.cancelled && callbacks_.cancelled();
}

std::vector<std::uint8_t> TimelineRenderRuntime::pack_frame(const RenderVideoFrame& frame) {
  if (frame.gpu_resident() || !frame.valid()) return {};
  std::vector<std::uint8_t> bytes(frame.rgba.size() * sizeof(float));
  if (!bytes.empty()) std::memcpy(bytes.data(), frame.rgba.data(), bytes.size());
  return bytes;
}

std::optional<RenderVideoFrame> TimelineRenderRuntime::unpack_frame(
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t width,
    std::uint32_t height,
    std::string provenance) {
  const auto pixels = static_cast<std::size_t>(width) * height;
  if (pixels > std::numeric_limits<std::size_t>::max() / (4U * sizeof(float)))
    return std::nullopt;
  const auto expected = pixels * 4U * sizeof(float);
  if (bytes.size() != expected) return std::nullopt;
  RenderVideoFrame frame;
  frame.width = width;
  frame.height = height;
  frame.rgba.resize(expected / sizeof(float));
  if (!bytes.empty()) std::memcpy(frame.rgba.data(), bytes.data(), bytes.size());
  frame.provenance = std::move(provenance);
  return frame;
}

std::optional<RenderVideoFrame> TimelineRenderRuntime::get_gpu_cached(
    const RenderCacheKey& key) {
  std::scoped_lock lock(gpu_cache_mutex_);
  const auto found = gpu_cache_.find(key);
  if (found == gpu_cache_.end()) return std::nullopt;
  if (!found->second.frame.valid() || !found->second.frame.gpu_resident()) {
    gpu_cache_.erase(found);
    return std::nullopt;
  }
  found->second.stamp = ++gpu_cache_clock_;
  return found->second.frame;
}

void TimelineRenderRuntime::put_gpu_cached(RenderCacheKey key, RenderVideoFrame frame) {
  if (gpu_cache_capacity_ == 0 || !frame.gpu_resident() || !frame.valid()) return;
  std::scoped_lock lock(gpu_cache_mutex_);
  gpu_cache_[std::move(key)] = {std::move(frame), ++gpu_cache_clock_};
  evict_gpu_cache_locked();
}

void TimelineRenderRuntime::evict_gpu_cache_locked() {
  while (gpu_cache_.size() > gpu_cache_capacity_) {
    auto victim = gpu_cache_.end();
    for (auto iterator = gpu_cache_.begin(); iterator != gpu_cache_.end(); ++iterator) {
      if (victim == gpu_cache_.end() || iterator->second.stamp < victim->second.stamp)
        victim = iterator;
    }
    if (victim == gpu_cache_.end()) break;
    gpu_cache_.erase(victim);
  }
}

TimelineRenderResult TimelineRenderRuntime::render(TimelineExecutionMode mode,
                                                   std::int64_t timeline_us,
                                                   std::uint32_t width,
                                                   std::uint32_t height,
                                                   std::uint64_t timeline_revision,
                                                   std::uint64_t render_revision,
                                                   std::size_t audio_frames,
                                                   bool allow_proxy) {
  TimelineRenderResult result;
  if (width == 0 || height == 0 || !callbacks_.decode_video || !callbacks_.composite) {
    result.diagnostic = "invalid render runtime configuration";
    return result;
  }

  const auto plan = executor_.build_plan(mode, timeline_us, width, height,
                                         timeline_revision, render_revision);
  result.plan_identity = plan.identity;

  RenderVideoFrame composite;
  const bool gpu_path = static_cast<bool>(callbacks_.create_gpu_target);
  if (gpu_path) {
    auto target = callbacks_.create_gpu_target(width, height, timeline_us);
    if (!target || !target->gpu_resident() || !target->valid()) {
      result.diagnostic = "GPU target creation failed or returned a non-resident frame";
      return result;
    }
    composite = std::move(*target);
  } else {
    composite.width = width;
    composite.height = height;
    composite.rgba.assign(static_cast<std::size_t>(width) * height * 4U, 0.0F);
    composite.provenance = "timeline-clear:cpu-linear-rgba";
  }

  for (const auto& layer : plan.video_layers) {
    if (is_cancelled()) {
      result.cancelled = true;
      result.diagnostic = "cancelled during video execution";
      return result;
    }

    std::optional<RenderVideoFrame> frame;
    if (gpu_path) {
      frame = get_gpu_cached(layer.cache_key);
    } else if (auto cached = cache_.get(layer.cache_key)) {
      frame = unpack_frame(*cached, width, height, "memory-cache:" + layer.clip_id);
    }
    if (frame) ++result.cache_hits;

    if (!frame) {
      ++result.cache_misses;
      frame = callbacks_.decode_video(layer, allow_proxy);
      if (!frame || !frame->valid() || frame->width != width || frame->height != height) {
        result.diagnostic = "video decode failed for " + layer.clip_id;
        return result;
      }
      if (frame->gpu_resident() != gpu_path) {
        result.diagnostic = gpu_path
            ? "GPU timeline received a CPU frame for " + layer.clip_id
            : "CPU timeline received a GPU frame without a GPU target for " + layer.clip_id;
        return result;
      }
      ++result.decoded_video_layers;
      if (frame->provenance.find("proxy") != std::string::npos) result.used_proxy = true;
      if (callbacks_.apply_effects && !callbacks_.apply_effects(layer, *frame)) {
        result.diagnostic = "effect execution failed for " + layer.clip_id;
        return result;
      }
      if (!frame->valid()) {
        result.diagnostic = "effect execution produced an invalid frame for " + layer.clip_id;
        return result;
      }
      if (gpu_path) {
        put_gpu_cached(layer.cache_key, *frame);
      } else {
        const auto packed = pack_frame(*frame);
        if (packed.empty()) {
          result.diagnostic = "CPU frame cache serialization failed for " + layer.clip_id;
          return result;
        }
        cache_.put(layer.cache_key, packed);
      }
    }

    // GPU compositors consume layer.opacity and layer.transition_weight directly.
    // No pixel is read back or multiplied on the CPU. The CPU fallback retains
    // deterministic linear-float behavior for validation and unsupported devices.
    if (!gpu_path) {
      const auto weight = static_cast<float>(layer.opacity * layer.transition_weight);
      for (auto& value : frame->rgba) value *= weight;
    }

    if (!callbacks_.composite(layer, *frame, composite) || !composite.valid()) {
      result.diagnostic = "composite failed for " + layer.clip_id;
      return result;
    }
  }

  std::vector<std::vector<float>> audio_storage;
  std::vector<AudioMixInput> mix_inputs;
  if (callbacks_.decode_audio && audio_frames > 0) {
    audio_storage.reserve(plan.audio_layers.size());
    mix_inputs.reserve(plan.audio_layers.size());
    for (const auto& layer : plan.audio_layers) {
      if (is_cancelled()) {
        result.cancelled = true;
        result.diagnostic = "cancelled during audio execution";
        return result;
      }
      auto block = callbacks_.decode_audio(layer, audio_frames);
      if (!block || block->interleaved_stereo.size() < audio_frames * 2U) {
        result.diagnostic = "audio decode failed for " + layer.clip_id;
        return result;
      }
      result.audio.sample_rate = block->sample_rate;
      audio_storage.push_back(std::move(block->interleaved_stereo));
      mix_inputs.push_back({audio_storage.back().data(), audio_frames,
                            layer.gain, layer.pan, layer.muted});
    }
    result.audio.interleaved_stereo = mix_audio_stereo(mix_inputs, audio_frames).interleaved_stereo;
  }

  result.video = std::move(composite);
  result.gpu_resident = result.video.gpu_resident();
  result.success = true;
  result.diagnostic = gpu_path ? "completed:gpu-resident" : "completed:cpu-fallback";
  return result;
}

void TimelineRenderRuntime::invalidate_clip(const std::string& clip_id) {
  cache_.invalidate_clip(clip_id);
  std::scoped_lock lock(gpu_cache_mutex_);
  for (auto iterator = gpu_cache_.begin(); iterator != gpu_cache_.end();) {
    if (iterator->first.clip_id == clip_id) iterator = gpu_cache_.erase(iterator);
    else ++iterator;
  }
}

void TimelineRenderRuntime::clear_cache() noexcept {
  cache_.clear();
  try {
    std::scoped_lock lock(gpu_cache_mutex_);
    gpu_cache_.clear();
  } catch (...) {
  }
}

}  // namespace digitor
