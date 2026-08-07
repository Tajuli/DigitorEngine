#include "digitor/timeline_render_runtime.hpp"
#include "digitor/cpu_parallel_executor.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace digitor {
namespace {

std::size_t parallel_scale_rgba(std::vector<float>& rgba, float weight) {
  if (rgba.empty() || weight == 1.0F) return 1;
  constexpr std::size_t kMinValuesPerTask = 64U * 1024U;
  auto& executor = shared_cpu_executor();
  executor.parallel_for(
      rgba.size(), kMinValuesPerTask,
      [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) rgba[i] *= weight;
      });
  return std::max<std::size_t>(1, executor.telemetry().last_task_count);
}

bool exact_cpu_pixels_equal(const RenderVideoFrame& a,
                            const RenderVideoFrame& b) noexcept {
  if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size())
    return false;
  if (a.rgba.empty()) return true;
  return std::memcmp(a.rgba.data(), b.rgba.data(), a.rgba.size() * sizeof(float)) == 0;
}

}  // namespace

bool RenderVideoFrame::valid() const noexcept {
  if (width == 0 || height == 0) return false;
  if (gpu) {
    const auto& metadata = gpu->metadata();
    return rgba.empty() && gpu->ready() && metadata.width == width && metadata.height == height;
  }
  const auto pixels = static_cast<std::size_t>(width) * height;
  return pixels <= std::numeric_limits<std::size_t>::max() / 4U &&
         rgba.size() == pixels * 4U;
}

TimelineRenderRuntime::TimelineRenderRuntime(TimelineRenderExecutor executor,
                                             TimelineRenderCallbacks callbacks,
                                             std::size_t memory_cache_bytes,
                                             std::size_t gpu_cache_frames,
                                             std::size_t gpu_cache_bytes,
                                             RenderResidencyPolicy residency_policy)
    : executor_(std::move(executor)),
      callbacks_(std::move(callbacks)),
      cache_(memory_cache_bytes),
      gpu_cache_capacity_(gpu_cache_frames),
      gpu_cache_budget_bytes_(gpu_cache_bytes),
      residency_policy_(residency_policy) {}

std::size_t TimelineRenderRuntime::estimate_gpu_bytes(const RenderVideoFrame& frame) noexcept {
  if (!frame.gpu || !frame.width || !frame.height) return 0;
  std::size_t bytes_per_pixel = 0;
  switch (frame.gpu->metadata().format) {
    case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT: bytes_per_pixel = 8; break;
    case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT: bytes_per_pixel = 16; break;
    case DIGITOR_PIXEL_FORMAT_RGBA8_UNORM:
    case DIGITOR_PIXEL_FORMAT_BGRA8_UNORM: bytes_per_pixel = 4; break;
    default: return 0;
  }
  if (frame.width > std::numeric_limits<std::size_t>::max() / frame.height ||
      static_cast<std::size_t>(frame.width) * frame.height >
          std::numeric_limits<std::size_t>::max() / bytes_per_pixel) return 0;
  return static_cast<std::size_t>(frame.width) * frame.height * bytes_per_pixel;
}

bool TimelineRenderRuntime::gpu_frame_compatible(const RenderVideoFrame& frame,
                                                  const RenderVideoFrame& target,
                                                  std::int64_t timestamp_us,
                                                  std::string& diagnostic) noexcept {
  if (!frame.gpu || !target.gpu || !frame.rgba.empty() || !target.rgba.empty()) {
    diagnostic = "strict GPU boundary contains CPU pixel storage";
    return false;
  }
  if (frame.gpu->backend() == DIGITOR_RENDERER_CPU ||
      target.gpu->backend() == DIGITOR_RENDERER_CPU) {
    diagnostic = "GPU timeline received a CPU renderer frame";
    return false;
  }
  if (!frame.gpu->context_live() || !target.gpu->context_live()) {
    diagnostic = "GPU context destroyed or stale";
    return false;
  }
  if (frame.gpu->backend() != target.gpu->backend()) {
    diagnostic = "GPU backend mismatch";
    return false;
  }
  if (!frame.gpu->same_context(*target.gpu)) {
    diagnostic = "GPU device/context mismatch";
    return false;
  }
  const auto& metadata = frame.gpu->metadata();
  const auto& target_metadata = target.gpu->metadata();
  if (metadata.width != target_metadata.width || metadata.height != target_metadata.height) {
    diagnostic = "GPU dimensions mismatch";
    return false;
  }
  if (metadata.format != target_metadata.format) {
    diagnostic = "GPU working pixel format mismatch";
    return false;
  }
  if (metadata.color_metadata.empty() || target_metadata.color_metadata.empty()) {
    diagnostic = "GPU color metadata missing";
    return false;
  }
  if (metadata.timestamp != 0 && metadata.timestamp != timestamp_us) {
    diagnostic = "GPU timestamp mismatch";
    return false;
  }
  return true;
}

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
    gpu_cache_bytes_ -= found->second.estimated_bytes;
    gpu_cache_.erase(found);
    return std::nullopt;
  }
  found->second.stamp = ++gpu_cache_clock_;
  return found->second.frame;
}

void TimelineRenderRuntime::put_gpu_cached(RenderCacheKey key, RenderVideoFrame frame) {
  const auto bytes = estimate_gpu_bytes(frame);
  if (gpu_cache_capacity_ == 0 || gpu_cache_budget_bytes_ == 0 || bytes == 0 ||
      bytes > gpu_cache_budget_bytes_ || !frame.gpu_resident() || !frame.valid()) return;
  std::scoped_lock lock(gpu_cache_mutex_);
  if (auto existing = gpu_cache_.find(key); existing != gpu_cache_.end()) {
    gpu_cache_bytes_ -= existing->second.estimated_bytes;
    gpu_cache_.erase(existing);
  }
  gpu_cache_bytes_ += bytes;
  gpu_cache_[std::move(key)] = {std::move(frame), ++gpu_cache_clock_, bytes};
  evict_gpu_cache_locked();
}

void TimelineRenderRuntime::evict_gpu_cache_locked() {
  while (gpu_cache_.size() > gpu_cache_capacity_ || gpu_cache_bytes_ > gpu_cache_budget_bytes_) {
    auto victim = gpu_cache_.end();
    for (auto iterator = gpu_cache_.begin(); iterator != gpu_cache_.end(); ++iterator) {
      if (callbacks_.gpu_frame_evictable && iterator->second.frame.gpu &&
          !callbacks_.gpu_frame_evictable(*iterator->second.frame.gpu)) continue;
      if (victim == gpu_cache_.end() || iterator->second.stamp < victim->second.stamp)
        victim = iterator;
    }
    if (victim == gpu_cache_.end()) break;
    gpu_cache_bytes_ -= victim->second.estimated_bytes;
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

  const bool gpu_available = static_cast<bool>(callbacks_.create_gpu_target);
  if (residency_policy_ == RenderResidencyPolicy::require_gpu && !gpu_available) {
    result.diagnostic = "strict GPU render requested but no GPU target provider is available";
    return result;
  }
  const bool gpu_path = residency_policy_ != RenderResidencyPolicy::cpu_only && gpu_available;
  result.used_cpu_fallback =
      residency_policy_ == RenderResidencyPolicy::gpu_first_cpu_fallback && !gpu_path;

  RenderVideoFrame composite;
  if (gpu_path) {
    auto target = callbacks_.create_gpu_target(width, height, timeline_us);
    if (!target || !target->gpu_resident() || !target->valid() ||
        !target->gpu || target->gpu->backend() == DIGITOR_RENDERER_CPU) {
      result.diagnostic = "selected GPU target creation failed; runtime CPU fallback is prohibited";
      return result;
    }
    composite = std::move(*target);
    if (!composite.rgba.empty() || !composite.gpu->context_live()) {
      result.diagnostic = "GPU target violates strict residency/context contract";
      return result;
    }
  } else {
    composite.width = width;
    composite.height = height;
    composite.rgba.assign(static_cast<std::size_t>(width) * height * 4U, 0.0F);
    composite.provenance = "timeline-clear:cpu-linear-rgba";
    result.cpu_worker_threads = 1;
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
      if (gpu_path && !gpu_frame_compatible(*frame, composite, layer.source_time_us,
                                             result.diagnostic)) {
        result.diagnostic += " for " + layer.clip_id;
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
      if (gpu_path && !gpu_frame_compatible(*frame, composite, layer.source_time_us,
                                             result.diagnostic)) {
        result.diagnostic = "effect output " + result.diagnostic + " for " + layer.clip_id;
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

    if (!gpu_path) {
      const auto weight = static_cast<float>(layer.opacity * layer.transition_weight);
      result.cpu_worker_threads = std::max(result.cpu_worker_threads,
                                           parallel_scale_rgba(frame->rgba, weight));
    }

    if (!callbacks_.composite(layer, *frame, composite) || !composite.valid()) {
      result.diagnostic = "composite failed for " + layer.clip_id;
      return result;
    }
    if (gpu_path && (!composite.rgba.empty() || !composite.gpu->context_live())) {
      result.diagnostic = "compositor returned CPU storage or a stale GPU frame";
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
  result.diagnostic = gpu_path ? "completed:gpu-resident"
                               : result.used_cpu_fallback
                                     ? "completed:multithreaded-cpu-fallback"
                                     : "completed:cpu-only";
  return result;
}

PreviewExportParityResult TimelineRenderRuntime::verify_preview_export_parity(
    std::int64_t timeline_us,
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t timeline_revision,
    std::uint64_t render_revision,
    std::size_t audio_frames) {
  PreviewExportParityResult parity;
  auto preview = render(TimelineExecutionMode::preview, timeline_us, width, height,
                        timeline_revision, render_revision, audio_frames, false);
  if (!preview.success) {
    parity.diagnostic = "preview parity render failed: " + preview.diagnostic;
    return parity;
  }
  auto export_frame = render(TimelineExecutionMode::export_render, timeline_us, width, height,
                             timeline_revision, render_revision, audio_frames, false);
  if (!export_frame.success) {
    parity.diagnostic = "export parity render failed: " + export_frame.diagnostic;
    return parity;
  }
  if (preview.video.storage() != export_frame.video.storage()) {
    parity.verified = true;
    parity.equivalent = false;
    parity.diagnostic = "preview/export residency differs";
    return parity;
  }
  if (!preview.video.gpu_resident()) {
    parity.verified = true;
    parity.equivalent = exact_cpu_pixels_equal(preview.video, export_frame.video);
    parity.diagnostic = parity.equivalent ? "exact CPU pixel parity verified"
                                          : "CPU preview/export pixels differ";
    return parity;
  }
  if (!callbacks_.compare_gpu_frames) {
    parity.diagnostic = "GPU pixel parity requires a backend validation comparator";
    return parity;
  }
  std::string diagnostic;
  parity.equivalent = callbacks_.compare_gpu_frames(preview.video, export_frame.video, diagnostic);
  parity.verified = true;
  parity.diagnostic = diagnostic.empty()
                          ? (parity.equivalent ? "GPU pixel parity verified"
                                               : "GPU preview/export pixels differ")
                          : std::move(diagnostic);
  return parity;
}

void TimelineRenderRuntime::invalidate_clip(const std::string& clip_id) {
  cache_.invalidate_clip(clip_id);
  std::scoped_lock lock(gpu_cache_mutex_);
  for (auto iterator = gpu_cache_.begin(); iterator != gpu_cache_.end();) {
    if (iterator->first.clip_id == clip_id) {
      gpu_cache_bytes_ -= iterator->second.estimated_bytes;
      iterator = gpu_cache_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

void TimelineRenderRuntime::clear_cache() noexcept {
  cache_.clear();
  try {
    std::scoped_lock lock(gpu_cache_mutex_);
    gpu_cache_.clear();
    gpu_cache_bytes_ = 0;
  } catch (...) {
  }
}

void TimelineRenderRuntime::notify_gpu_memory_pressure(std::size_t new_budget_bytes) noexcept {
  try {
    std::scoped_lock lock(gpu_cache_mutex_);
    gpu_cache_budget_bytes_ = new_budget_bytes;
    evict_gpu_cache_locked();
  } catch (...) {
  }
}

std::size_t TimelineRenderRuntime::gpu_cache_bytes() const noexcept {
  std::scoped_lock lock(gpu_cache_mutex_);
  return gpu_cache_bytes_;
}

}  // namespace digitor
