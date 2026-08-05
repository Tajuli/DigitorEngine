#include "digitor/production_timeline_render_orchestrator.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace digitor {
namespace {

std::uint64_t append_digest(std::uint64_t hash, const void* data,
                            std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return hash;
}

bool valid_request(const TimelineRenderRequest& request) noexcept {
  const auto mode = static_cast<std::uint32_t>(request.mode);
  return mode <= static_cast<std::uint32_t>(TimelineRenderMode::export_frame) &&
         std::isfinite(request.timeline_time_seconds) &&
         request.timeline_time_seconds >= 0.0 && request.project_revision > 0u;
}

}  // namespace

std::uint64_t timeline_render_digest(const TimelineRenderRequest& request,
                                     std::uint64_t video_digest,
                                     std::uint64_t audio_digest) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  hash = append_digest(hash, &request.timeline_time_seconds,
                       sizeof(request.timeline_time_seconds));
  hash = append_digest(hash, &request.project_revision,
                       sizeof(request.project_revision));
  hash = append_digest(hash, &request.frame_index, sizeof(request.frame_index));
  hash = append_digest(hash, &video_digest, sizeof(video_digest));
  hash = append_digest(hash, &audio_digest, sizeof(audio_digest));
  return hash;
}

TimelineRenderResult render_timeline_frame(
    const TimelineRenderRequest& request,
    const TimelineStageExecutor& executor) noexcept {
  TimelineRenderResult result;
  if (!valid_request(request) || !executor) {
    return result;
  }

  constexpr std::array<TimelineRenderStage, 7> video_stages{
      TimelineRenderStage::keyframe_evaluation,
      TimelineRenderStage::source_decode,
      TimelineRenderStage::transform_crop,
      TimelineRenderStage::effects,
      TimelineRenderStage::transition,
      TimelineRenderStage::compositing,
      TimelineRenderStage::output};

  std::uint64_t dependency = 0u;
  for (const auto stage : video_stages) {
    const auto stage_result = executor(stage, request, dependency);
    if (stage_result.status != TimelineRenderStatus::ready ||
        stage_result.digest == 0u) {
      result.status = stage_result.status == TimelineRenderStatus::backend_unavailable
                          ? TimelineRenderStatus::backend_unavailable
                          : TimelineRenderStatus::stage_failed;
      result.failed_stage = stage;
      return result;
    }
    dependency = stage_result.digest;
  }
  result.video_digest = dependency;

  if (request.include_audio) {
    const auto audio = executor(TimelineRenderStage::audio_mix, request, 0u);
    if (audio.status != TimelineRenderStatus::ready || audio.digest == 0u) {
      result.status = TimelineRenderStatus::stage_failed;
      result.failed_stage = TimelineRenderStage::audio_mix;
      return result;
    }
    result.audio_digest = audio.digest;
  }

  result.render_digest =
      timeline_render_digest(request, result.video_digest, result.audio_digest);
  result.status = TimelineRenderStatus::ready;
  return result;
}

}  // namespace digitor

extern "C" std::uint32_t digitor_render_timeline_frame(
    const DigitorTimelineRenderRequest* request,
    DigitorTimelineStageCallback callback,
    void* user_data,
    DigitorTimelineRenderResult* output) {
  if (!request || !callback || !output) {
    return 1u;
  }

  digitor::TimelineRenderRequest native;
  native.mode = static_cast<digitor::TimelineRenderMode>(request->mode);
  native.timeline_time_seconds = request->timeline_time_seconds;
  native.project_revision = request->project_revision;
  native.frame_index = request->frame_index;
  native.gpu_selected = request->gpu_selected != 0u;
  native.include_audio = request->include_audio != 0u;

  const auto result = digitor::render_timeline_frame(
      native,
      [callback, user_data, request](digitor::TimelineRenderStage stage,
                                     const digitor::TimelineRenderRequest&,
                                     std::uint64_t dependency) {
        std::uint64_t digest = 0u;
        const auto status = callback(static_cast<std::uint32_t>(stage), request,
                                     dependency, &digest, user_data);
        return digitor::TimelineStageResult{
            static_cast<digitor::TimelineRenderStatus>(status), digest};
      });

  output->status = static_cast<std::uint32_t>(result.status);
  output->failed_stage = static_cast<std::uint32_t>(result.failed_stage);
  output->video_digest = result.video_digest;
  output->audio_digest = result.audio_digest;
  output->render_digest = result.render_digest;
  return result.status == digitor::TimelineRenderStatus::ready ? 0u : 2u;
}