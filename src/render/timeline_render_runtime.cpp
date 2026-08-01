#include "digitor/timeline_render_runtime.hpp"

#include <algorithm>
#include <cstring>

namespace digitor {

TimelineRenderRuntime::TimelineRenderRuntime(TimelineRenderExecutor executor,
                                             TimelineRenderCallbacks callbacks,
                                             std::size_t memory_cache_bytes)
    : executor_(std::move(executor)), callbacks_(std::move(callbacks)), cache_(memory_cache_bytes) {}

bool TimelineRenderRuntime::is_cancelled() const {
  return callbacks_.cancelled && callbacks_.cancelled();
}

std::vector<std::uint8_t> TimelineRenderRuntime::pack_frame(const RenderVideoFrame& frame) {
  std::vector<std::uint8_t> bytes(frame.rgba.size() * sizeof(float));
  if (!bytes.empty()) std::memcpy(bytes.data(), frame.rgba.data(), bytes.size());
  return bytes;
}

std::optional<RenderVideoFrame> TimelineRenderRuntime::unpack_frame(
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t width,
    std::uint32_t height,
    std::string provenance) {
  const auto expected = static_cast<std::size_t>(width) * height * 4U * sizeof(float);
  if (bytes.size() != expected) return std::nullopt;
  RenderVideoFrame frame;
  frame.width = width;
  frame.height = height;
  frame.rgba.resize(expected / sizeof(float));
  if (!bytes.empty()) std::memcpy(frame.rgba.data(), bytes.data(), bytes.size());
  frame.provenance = std::move(provenance);
  return frame;
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
  composite.width = width;
  composite.height = height;
  composite.rgba.assign(static_cast<std::size_t>(width) * height * 4U, 0.0F);
  composite.provenance = "timeline-clear";

  for (const auto& layer : plan.video_layers) {
    if (is_cancelled()) {
      result.cancelled = true;
      result.diagnostic = "cancelled during video execution";
      return result;
    }
    std::optional<RenderVideoFrame> frame;
    if (auto cached = cache_.get(layer.cache_key)) {
      frame = unpack_frame(*cached, width, height, "memory-cache:" + layer.clip_id);
      if (frame) ++result.cache_hits;
    }
    if (!frame) {
      ++result.cache_misses;
      frame = callbacks_.decode_video(layer, allow_proxy);
      if (!frame || frame->width != width || frame->height != height ||
          frame->rgba.size() != composite.rgba.size()) {
        result.diagnostic = "video decode failed for " + layer.clip_id;
        return result;
      }
      ++result.decoded_video_layers;
      if (frame->provenance.find("proxy") != std::string::npos) result.used_proxy = true;
      if (callbacks_.apply_effects && !callbacks_.apply_effects(layer, *frame)) {
        result.diagnostic = "effect execution failed for " + layer.clip_id;
        return result;
      }
      cache_.put(layer.cache_key, pack_frame(*frame));
    }
    for (auto& value : frame->rgba) {
      value = static_cast<float>(value * layer.opacity * layer.transition_weight);
    }
    if (!callbacks_.composite(layer, *frame, composite)) {
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
      mix_inputs.push_back({audio_storage.back().data(), audio_frames, layer.gain, layer.pan, layer.muted});
    }
    result.audio.interleaved_stereo = mix_audio_stereo(mix_inputs, audio_frames).interleaved_stereo;
  }

  result.video = std::move(composite);
  result.success = true;
  result.diagnostic = "completed";
  return result;
}

void TimelineRenderRuntime::invalidate_clip(const std::string& clip_id) {
  cache_.invalidate_clip(clip_id);
}

void TimelineRenderRuntime::clear_cache() noexcept { cache_.clear(); }

}  // namespace digitor
