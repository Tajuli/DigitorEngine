#pragma once

#include "digitor/professional_timeline_suite.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class AutomationProperty : std::uint8_t {
  opacity,
  volume,
  pan,
  position_x,
  position_y,
  scale,
  rotation
};

struct TimelineAutomationLane {
  std::string id;
  std::string clip_id;
  AutomationProperty property{AutomationProperty::opacity};
  AutomationCurve curve;
  bool enabled{true};
};

struct TimelineTransitionLane {
  std::string id;
  std::string outgoing_clip_id;
  std::string incoming_clip_id;
  TransitionSpec transition;
  bool enabled{true};
};

struct TimelineTrackGroup {
  std::string id;
  std::vector<std::string> track_ids;
  bool sync_lock{true};
  bool enabled{true};
};

struct MulticamCut {
  std::int64_t timeline_us{};
  std::size_t angle_index{};
};

struct MulticamGroup {
  std::string id;
  std::vector<std::string> angle_clip_ids;
  std::vector<MulticamCut> cuts;
  bool enabled{true};
};

struct NestedTimelineSequence {
  std::string id;
  TimelineProjectModel project;
};

struct TimelineCompletionProject {
  TimelineProjectModel timeline;
  std::vector<TimelineTrackGroup> track_groups;
  std::vector<TimelineAutomationLane> automation_lanes;
  std::vector<TimelineTransitionLane> transition_lanes;
  std::vector<MulticamGroup> multicam_groups;
  std::vector<NestedTimelineSequence> nested_sequences;
  std::uint64_t revision{};
};

struct TimelineCompletionSample {
  RenderPlan render_plan;
  std::unordered_map<std::string, double> automation_values;
  std::unordered_map<std::string, TransitionSample> transition_values;
  std::unordered_map<std::string, std::string> active_multicam_angles;
  std::uint64_t revision{};
};

class TimelineCompletionEngine {
 public:
  explicit TimelineCompletionEngine(TimelineCompletionProject project = {});

  [[nodiscard]] const TimelineCompletionProject& project() const noexcept;
  [[nodiscard]] bool validate() const noexcept;
  [[nodiscard]] TimelineCompletionSample sample(std::int64_t timeline_us) const;

  bool remove_track(const std::string& track_id,
                    TrackRemovalPolicy policy = TrackRemovalPolicy::reject_if_not_empty);
  bool set_track_group(TimelineTrackGroup group);
  bool remove_track_group(const std::string& group_id);
  bool move_track_group(const std::string& group_id, std::int64_t delta_us);

  bool set_automation_lane(TimelineAutomationLane lane);
  bool remove_automation_lane(const std::string& lane_id);
  bool set_transition_lane(TimelineTransitionLane lane);
  bool remove_transition_lane(const std::string& lane_id);

  bool set_multicam_group(MulticamGroup group);
  bool switch_multicam_angle(const std::string& group_id,
                             std::int64_t timeline_us,
                             std::size_t angle_index);
  bool remove_multicam_group(const std::string& group_id);

  bool set_nested_sequence(NestedTimelineSequence sequence);
  bool remove_nested_sequence(const std::string& sequence_id);
  [[nodiscard]] std::optional<TimelineResolvedFrame> resolve_nested(
      const std::string& sequence_id,
      std::int64_t timeline_us) const;

  [[nodiscard]] std::string serialize() const;
  [[nodiscard]] static std::optional<TimelineCompletionProject> deserialize(
      const std::string& text);

 private:
  TimelineCompletionProject project_;
  void bump_revision() noexcept;
};

}  // namespace digitor
