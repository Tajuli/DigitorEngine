#pragma once

#include "digitor/professional_timeline_suite.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class TimelineExecutionMode { preview, export_render };

struct VisualTransform {
  double position_x{};
  double position_y{};
  double scale_x{1.0};
  double scale_y{1.0};
  double rotation_degrees{};
  double anchor_x{0.5};
  double anchor_y{0.5};
  double crop_left{};
  double crop_top{};
  double crop_right{};
  double crop_bottom{};
};

struct ClipExecutionOverrides {
  double opacity{1.0};
  double volume{1.0};
  double pan{};
  std::uint64_t grade_revision{};
  TransitionSpec transition_in{};
  TransitionSpec transition_out{};
  AutomationCurve opacity_curve;
  AutomationCurve volume_curve;
  AutomationCurve pan_curve;

  // Shared visual controls for video, still-image, text and generated clips.
  // Curves are evaluated at clip-local time so moving the clip on the timeline
  // does not change its animation.
  VisualTransform transform{};
  AutomationCurve position_x_curve;
  AutomationCurve position_y_curve;
  AutomationCurve scale_x_curve;
  AutomationCurve scale_y_curve;
  AutomationCurve rotation_curve;
  AutomationCurve anchor_x_curve;
  AutomationCurve anchor_y_curve;
  AutomationCurve crop_left_curve;
  AutomationCurve crop_top_curve;
  AutomationCurve crop_right_curve;
  AutomationCurve crop_bottom_curve;

  // Still images use the same visual execution layer as video while retaining
  // one immutable source frame. This freezes decode/cache source time without
  // bypassing transform, node, effect, transition, preview or export stages.
  bool static_visual_source{};
  std::int64_t static_source_time_us{};
};

struct VideoExecutionLayer {
  std::string clip_id;
  std::size_t track_index{};
  std::int64_t timeline_us{};
  std::int64_t clip_local_time_us{};
  std::int64_t source_time_us{};
  double opacity{1.0};
  double transition_weight{1.0};
  VisualTransform transform{};
  RenderCacheKey cache_key;
};

struct AudioExecutionLayer {
  std::string clip_id;
  std::size_t track_index{};
  std::int64_t timeline_us{};
  std::int64_t source_time_us{};
  double gain{1.0};
  double pan{};
  bool muted{};
};

struct TimelineExecutionPlan {
  TimelineExecutionMode mode{TimelineExecutionMode::preview};
  std::int64_t timeline_us{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t timeline_revision{};
  std::uint64_t render_revision{};
  std::vector<VideoExecutionLayer> video_layers;
  std::vector<AudioExecutionLayer> audio_layers;
  std::string identity;
};

class TimelineRenderExecutor {
 public:
  TimelineRenderExecutor(TimelineProjectModel project,
                         std::unordered_map<std::string, ClipExecutionOverrides> overrides = {});

  [[nodiscard]] TimelineExecutionPlan build_plan(TimelineExecutionMode mode,
                                                 std::int64_t timeline_us,
                                                 std::uint32_t width,
                                                 std::uint32_t height,
                                                 std::uint64_t timeline_revision,
                                                 std::uint64_t render_revision) const;

  [[nodiscard]] bool preview_export_equivalent(std::int64_t timeline_us,
                                               std::uint32_t width,
                                               std::uint32_t height,
                                               std::uint64_t timeline_revision,
                                               std::uint64_t render_revision) const;

 private:
  TimelineProjectModel project_;
  std::unordered_map<std::string, ClipExecutionOverrides> overrides_;
};

}  // namespace digitor
