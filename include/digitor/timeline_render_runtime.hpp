#pragma once

#include "digitor/timeline_render_execution.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace digitor {

struct RenderVideoFrame {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<float> rgba;
  std::string provenance;
};

struct RenderAudioBlock {
  std::uint32_t sample_rate{48000};
  std::vector<float> interleaved_stereo;
};

struct TimelineRenderResult {
  bool success{};
  bool cancelled{};
  bool used_proxy{};
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
  std::function<bool()> cancelled;
};

class TimelineRenderRuntime {
 public:
  TimelineRenderRuntime(TimelineRenderExecutor executor,
                        TimelineRenderCallbacks callbacks,
                        std::size_t memory_cache_bytes = 64U * 1024U * 1024U);

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
  TimelineRenderExecutor executor_;
  TimelineRenderCallbacks callbacks_;
  TimelineFrameCache cache_;

  [[nodiscard]] bool is_cancelled() const;
  [[nodiscard]] static std::vector<std::uint8_t> pack_frame(const RenderVideoFrame& frame);
  [[nodiscard]] static std::optional<RenderVideoFrame> unpack_frame(
      const std::vector<std::uint8_t>& bytes,
      std::uint32_t width,
      std::uint32_t height,
      std::string provenance);
};

}  // namespace digitor
