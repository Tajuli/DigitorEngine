#pragma once

#include "digitor/professional_timeline_suite.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class TimelineExecutionMode { preview, export_render };

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
};

struct VideoExecutionLayer {
  std::string clip_id;
  std::size_t track_index{};
  std::int64_t timeline_us{};
  std::int64_t source_time_us{};
  double opacity{1.0};
  double transition_weight{1.0};
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
