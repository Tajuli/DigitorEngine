#pragma once

#include "digitor/gpu_frame.hpp"
#include "digitor/timeline_render_execution.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class RenderFrameStorage : std::uint8_t { cpu_linear_rgba, gpu_resident };

struct RenderVideoFrame {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<float> rgba;
  ProcessedGpuFramePtr gpu;
  std::string provenance;

  [[nodiscard]] RenderFrameStorage storage() const noexcept {
    return gpu ? RenderFrameStorage::gpu_resident : RenderFrameStorage::cpu_linear_rgba;
  }
  [[nodiscard]] bool gpu_resident() const noexcept { return static_cast<bool>(gpu); }
  [[nodiscard]] bool valid() const noexcept;
};

struct RenderAudioBlock {
  std::uint32_t sample_rate{48000};
  std::vector<float> interleaved_stereo;
};

struct TimelineRenderResult {
  bool success{};
  bool cancelled{};
  bool used_proxy{};
  bool gpu_resident{};
  std::size_t decoded_video_layers{};
  std::size_t cache_hits{};
  std::size_t cache_misses{};
  RenderVideoFrame video;
  RenderAudioBlock audio;
  std::string plan_identity;
  std::string diagnostic;
};

struct TimelineRenderCallbacks {
  std::function<std::optional<RenderVideoFrame>(const VideoExecutionLayer&, bool allow_proxy)> decode_video;
  std::function<std::optional<RenderAudioBlock>(const AudioExecutionLayer&, std::size_t frames)> decode_audio;
  std::function<bool(const VideoExecutionLayer&, RenderVideoFrame&)> apply_effects;
  std::function<bool(const VideoExecutionLayer&, const RenderVideoFrame&, RenderVideoFrame&)> composite;
  std::function<std::optional<RenderVideoFrame>(std::uint32_t width,
                                                std::uint32_t height,
                                                std::int64_t timestamp_us)> create_gpu_target;
  std::function<bool()> cancelled;
};

class TimelineRenderRuntime {
 public:
  TimelineRenderRuntime(TimelineRenderExecutor executor,
                        TimelineRenderCallbacks callbacks,
                        std::size_t memory_cache_bytes = 64U * 1024U * 1024U,
                        std::size_t gpu_cache_frames = 16U);

  [[nodiscard]] TimelineRenderResult render(TimelineExecutionMode mode,
                                            std::int64_t timeline_us,
                                            std::uint32_t width,
                                            std::uint32_t height,
                                            std::uint64_t timeline_revision,
                                            std::uint64_t render_revision,
                                            std::size_t audio_frames = 1024,
                                            bool allow_proxy = true);

  void invalidate_clip(const std::string& clip_id);
  void clear_cache() noexcept;

 private:
  struct GpuCacheEntry {
    RenderVideoFrame frame;
    std::uint64_t stamp{};
  };

  TimelineRenderExecutor executor_;
  TimelineRenderCallbacks callbacks_;
  TimelineFrameCache cache_;
  std::size_t gpu_cache_capacity_{};
  std::uint64_t gpu_cache_clock_{};
  std::unordered_map<RenderCacheKey, GpuCacheEntry, RenderCacheKeyHash> gpu_cache_;
  mutable std::mutex gpu_cache_mutex_;

  [[nodiscard]] bool is_cancelled() const;
  [[nodiscard]] static std::vector<std::uint8_t> pack_frame(const RenderVideoFrame& frame);
  [[nodiscard]] static std::optional<RenderVideoFrame> unpack_frame(
      const std::vector<std::uint8_t>& bytes,
      std::uint32_t width,
      std::uint32_t height,
      std::string provenance);
  [[nodiscard]] std::optional<RenderVideoFrame> get_gpu_cached(const RenderCacheKey& key);
  void put_gpu_cached(RenderCacheKey key, RenderVideoFrame frame);
  void evict_gpu_cache_locked();
};

}  // namespace digitor
