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

enum class RenderResidencyPolicy : std::uint8_t {
  gpu_first_cpu_fallback,
  require_gpu,
  cpu_only,
};

struct RenderVideoFrame {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<float> rgba;
  std::string provenance;
  ProcessedGpuFramePtr gpu;

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
  bool used_cpu_fallback{};
  std::size_t cpu_worker_threads{};
  std::size_t decoded_video_layers{};
  std::size_t cache_hits{};
  std::size_t cache_misses{};
  RenderVideoFrame video;
  RenderAudioBlock audio;
  std::string plan_identity;
  std::string diagnostic;
};

struct PreviewExportParityResult {
  bool verified{};
  bool equivalent{};
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
  std::function<bool(const ProcessedGpuFrame&)> gpu_frame_evictable;
  // Production GPU parity must be measured by the backend using a deterministic
  // validation readback/hash path. Absence of this callback is never treated as
  // proof that preview and export pixels are equivalent.
  std::function<bool(const RenderVideoFrame&, const RenderVideoFrame&, std::string& diagnostic)>
      compare_gpu_frames;
};

class TimelineRenderRuntime {
 public:
  TimelineRenderRuntime(TimelineRenderExecutor executor,
                        TimelineRenderCallbacks callbacks,
                        std::size_t memory_cache_bytes = 64U * 1024U * 1024U,
                        std::size_t gpu_cache_frames = 16U,
                        std::size_t gpu_cache_bytes = 256U * 1024U * 1024U,
                        RenderResidencyPolicy residency_policy =
                            RenderResidencyPolicy::gpu_first_cpu_fallback);

  [[nodiscard]] TimelineRenderResult render(TimelineExecutionMode mode,
                                            std::int64_t timeline_us,
                                            std::uint32_t width,
                                            std::uint32_t height,
                                            std::uint64_t timeline_revision,
                                            std::uint64_t render_revision,
                                            std::size_t audio_frames = 1024,
                                            bool allow_proxy = true);

  [[nodiscard]] PreviewExportParityResult verify_preview_export_parity(
      std::int64_t timeline_us,
      std::uint32_t width,
      std::uint32_t height,
      std::uint64_t timeline_revision,
      std::uint64_t render_revision,
      std::size_t audio_frames = 0);

  void invalidate_clip(const std::string& clip_id);
  void clear_cache() noexcept;
  void notify_gpu_memory_pressure(std::size_t new_budget_bytes) noexcept;
  [[nodiscard]] std::size_t gpu_cache_bytes() const noexcept;
  [[nodiscard]] RenderResidencyPolicy residency_policy() const noexcept {
    return residency_policy_;
  }

 private:
  struct GpuCacheEntry {
    RenderVideoFrame frame;
    std::uint64_t stamp{};
    std::size_t estimated_bytes{};
  };

  TimelineRenderExecutor executor_;
  TimelineRenderCallbacks callbacks_;
  TimelineFrameCache cache_;
  std::size_t gpu_cache_capacity_{};
  std::size_t gpu_cache_budget_bytes_{};
  std::size_t gpu_cache_bytes_{};
  std::uint64_t gpu_cache_clock_{};
  std::unordered_map<RenderCacheKey, GpuCacheEntry, RenderCacheKeyHash> gpu_cache_;
  mutable std::mutex gpu_cache_mutex_;
  RenderResidencyPolicy residency_policy_{RenderResidencyPolicy::gpu_first_cpu_fallback};

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
  [[nodiscard]] static std::size_t estimate_gpu_bytes(const RenderVideoFrame&) noexcept;
  [[nodiscard]] static bool gpu_frame_compatible(const RenderVideoFrame& frame,
                                                 const RenderVideoFrame& target,
                                                 std::int64_t timestamp_us,
                                                 std::string& diagnostic) noexcept;
};

}  // namespace digitor
