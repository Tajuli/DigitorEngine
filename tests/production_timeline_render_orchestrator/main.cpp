#include "digitor/production_timeline_render_orchestrator.hpp"

#include <cstdint>

namespace {

std::uint64_t stage_digest(std::uint32_t stage, std::uint64_t dependency) {
  return 100u + stage * 17u + dependency;
}

std::uint32_t c_callback(std::uint32_t stage,
                         const DigitorTimelineRenderRequest*,
                         std::uint64_t dependency,
                         std::uint64_t* digest,
                         void*) {
  *digest = stage_digest(stage, dependency);
  return static_cast<std::uint32_t>(digitor::TimelineRenderStatus::ready);
}

}  // namespace

int main() {
  using namespace digitor;

  TimelineRenderRequest preview;
  preview.timeline_time_seconds = 1.25;
  preview.project_revision = 42u;
  preview.frame_index = 30u;
  preview.include_audio = true;

  const auto executor = [](TimelineRenderStage stage,
                           const TimelineRenderRequest&,
                           std::uint64_t dependency) {
    return TimelineStageResult{
        TimelineRenderStatus::ready,
        stage_digest(static_cast<std::uint32_t>(stage), dependency)};
  };

  const auto preview_result = render_timeline_frame(preview, executor);
  TimelineRenderRequest export_request = preview;
  export_request.mode = TimelineRenderMode::export_frame;
  const auto export_result = render_timeline_frame(export_request, executor);

  if (preview_result.status != TimelineRenderStatus::ready ||
      export_result.status != TimelineRenderStatus::ready) {
    return 1;
  }
  if (preview_result.video_digest != export_result.video_digest ||
      preview_result.audio_digest != export_result.audio_digest ||
      preview_result.render_digest != export_result.render_digest) {
    return 2;
  }

  const auto failed = render_timeline_frame(
      preview,
      [](TimelineRenderStage stage, const TimelineRenderRequest&,
         std::uint64_t dependency) {
        if (stage == TimelineRenderStage::effects) {
          return TimelineStageResult{TimelineRenderStatus::backend_unavailable,
                                     0u};
        }
        return TimelineStageResult{
            TimelineRenderStatus::ready,
            stage_digest(static_cast<std::uint32_t>(stage), dependency)};
      });
  if (failed.status != TimelineRenderStatus::backend_unavailable ||
      failed.failed_stage != TimelineRenderStage::effects) {
    return 3;
  }

  TimelineRenderRequest invalid = preview;
  invalid.project_revision = 0u;
  if (render_timeline_frame(invalid, executor).status !=
      TimelineRenderStatus::invalid) {
    return 4;
  }

  DigitorTimelineRenderRequest c_request{};
  c_request.mode = 0u;
  c_request.timeline_time_seconds = 1.25;
  c_request.project_revision = 42u;
  c_request.frame_index = 30u;
  c_request.gpu_selected = 1u;
  c_request.include_audio = 1u;
  DigitorTimelineRenderResult c_result{};
  if (digitor_render_timeline_frame(&c_request, c_callback, nullptr,
                                    &c_result) != 0u ||
      c_result.status != static_cast<std::uint32_t>(TimelineRenderStatus::ready) ||
      c_result.render_digest == 0u) {
    return 5;
  }

  return 0;
}