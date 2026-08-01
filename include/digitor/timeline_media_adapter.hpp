#pragma once

#include "digitor/timeline_render_runtime.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace digitor {

struct TimelineMediaSource {
  std::string clip_id;
  std::string original_path;
  std::string proxy_path;
  bool prefer_proxy_for_preview{true};
  std::int64_t stream_duration_us{};
};

struct MediaDecodeRequest {
  std::string clip_id;
  std::string path;
  std::int64_t source_time_us{};
  std::uint32_t width{};
  std::uint32_t height{};
  bool proxy{};
  TimelineExecutionMode mode{TimelineExecutionMode::preview};
};

struct MediaAdapterCallbacks {
  std::function<std::optional<RenderVideoFrame>(const MediaDecodeRequest&)> decode_video;
  std::function<std::optional<RenderAudioBlock>(const MediaDecodeRequest&, std::size_t)> decode_audio;
  std::function<bool(const VideoExecutionLayer&, RenderVideoFrame&)> apply_effects;
  std::function<bool(const VideoExecutionLayer&, const RenderVideoFrame&, RenderVideoFrame&)> composite;
  std::function<bool(const RenderVideoFrame&, const TimelineExecutionPlan&)> deliver_preview;
  std::function<bool(const RenderVideoFrame&, const TimelineExecutionPlan&)> deliver_export;
  std::function<bool()> cancelled;
};

class TimelineMediaAdapter {
 public:
  TimelineMediaAdapter(std::vector<TimelineMediaSource> sources, MediaAdapterCallbacks callbacks);

  [[nodiscard]] TimelineRenderCallbacks make_render_callbacks(TimelineExecutionMode mode,
                                                               std::uint32_t width,
                                                               std::uint32_t height,
                                                               bool allow_proxy) const;
  [[nodiscard]] bool deliver(const TimelineRenderResult& result,
                             const TimelineExecutionPlan& plan) const;
  [[nodiscard]] bool has_source(const std::string& clip_id) const noexcept;
  [[nodiscard]] std::optional<std::string> selected_path(const std::string& clip_id,
                                                         TimelineExecutionMode mode,
                                                         bool allow_proxy = true) const;

 private:
  std::unordered_map<std::string, TimelineMediaSource> sources_;
  MediaAdapterCallbacks callbacks_;
};

}  // namespace digitor
