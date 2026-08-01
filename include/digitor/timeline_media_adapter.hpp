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
  std::function<std::optional<RuntimeVideoFrame>(const MediaDecodeRequest&)> decode_video;
  std::function<std::optional<RuntimeAudioBlock>(const MediaDecodeRequest&, std::size_t)> decode_audio;
  std::function<bool(const RuntimeVideoFrame&, const TimelineExecutionPlan&)> deliver_preview;
  std::function<bool(const RuntimeVideoFrame&, const TimelineExecutionPlan&)> deliver_export;
};

struct TimelineMediaAdapterResult {
  bool success{};
  bool used_proxy{};
  bool delivered{};
  TimelineRenderResult render;
  std::string diagnostic;
};

class TimelineMediaAdapter {
 public:
  TimelineMediaAdapter(TimelineRenderRuntime& runtime,
                       std::vector<TimelineMediaSource> sources,
                       MediaAdapterCallbacks callbacks);

  [[nodiscard]] TimelineMediaAdapterResult execute(const TimelineExecutionPlan& plan,
                                                   std::size_t audio_frames,
                                                   const std::function<bool()>& cancelled = {});

  [[nodiscard]] bool has_source(const std::string& clip_id) const noexcept;
  [[nodiscard]] std::optional<std::string> selected_path(const std::string& clip_id,
                                                         TimelineExecutionMode mode) const;

 private:
  TimelineRenderRuntime& runtime_;
  std::unordered_map<std::string, TimelineMediaSource> sources_;
  MediaAdapterCallbacks callbacks_;
};

}  // namespace digitor
