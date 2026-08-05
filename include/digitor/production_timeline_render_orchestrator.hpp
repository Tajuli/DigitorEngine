#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace digitor {

enum class TimelineRenderStatus : std::uint32_t {
  invalid,
  ready,
  backend_unavailable,
  stage_failed
};

enum class TimelineRenderMode : std::uint32_t { preview, export_frame };

enum class TimelineRenderStage : std::uint32_t {
  keyframe_evaluation,
  source_decode,
  transform_crop,
  effects,
  transition,
  compositing,
  audio_mix,
  output
};

struct TimelineRenderRequest final {
  TimelineRenderMode mode{TimelineRenderMode::preview};
  double timeline_time_seconds{};
  std::uint64_t project_revision{};
  std::uint64_t frame_index{};
  bool gpu_selected{true};
  bool include_audio{true};
};

struct TimelineStageResult final {
  TimelineRenderStatus status{TimelineRenderStatus::invalid};
  std::uint64_t digest{};
};

struct TimelineRenderResult final {
  TimelineRenderStatus status{TimelineRenderStatus::invalid};
  TimelineRenderStage failed_stage{TimelineRenderStage::keyframe_evaluation};
  std::uint64_t video_digest{};
  std::uint64_t audio_digest{};
  std::uint64_t render_digest{};
};

using TimelineStageExecutor =
    std::function<TimelineStageResult(TimelineRenderStage,
                                      const TimelineRenderRequest&,
                                      std::uint64_t dependency_digest)>;

TimelineRenderResult render_timeline_frame(
    const TimelineRenderRequest& request,
    const TimelineStageExecutor& executor) noexcept;

std::uint64_t timeline_render_digest(const TimelineRenderRequest& request,
                                     std::uint64_t video_digest,
                                     std::uint64_t audio_digest) noexcept;

}  // namespace digitor

extern "C" {

struct DigitorTimelineRenderRequest {
  std::uint32_t mode;
  double timeline_time_seconds;
  std::uint64_t project_revision;
  std::uint64_t frame_index;
  std::uint32_t gpu_selected;
  std::uint32_t include_audio;
};

struct DigitorTimelineRenderResult {
  std::uint32_t status;
  std::uint32_t failed_stage;
  std::uint64_t video_digest;
  std::uint64_t audio_digest;
  std::uint64_t render_digest;
};

typedef std::uint32_t (*DigitorTimelineStageCallback)(
    std::uint32_t stage,
    const DigitorTimelineRenderRequest* request,
    std::uint64_t dependency_digest,
    std::uint64_t* stage_digest,
    void* user_data);

std::uint32_t digitor_render_timeline_frame(
    const DigitorTimelineRenderRequest* request,
    DigitorTimelineStageCallback callback,
    void* user_data,
    DigitorTimelineRenderResult* output);

}