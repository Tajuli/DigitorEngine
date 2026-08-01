#pragma once

#include "digitor/multitrack_timeline.hpp"
#include "digitor/professional_timeline_suite.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace digitor {

enum class TimelineInsertMode { insert, overwrite };
enum class TimelineEditKind { move, trim, split, roll, slip, slide, insert, overwrite, ripple_delete, lift, gap_close };

struct TimelineMarker { std::string id; std::int64_t time_us{}; std::int64_t duration_us{}; std::string name; std::string note; };
struct TimelineSelection { std::unordered_set<std::string> clip_ids; std::optional<std::string> primary_clip_id; };
struct TimelineEditResult { bool success{}; TimelineEditKind kind{TimelineEditKind::move}; std::vector<std::string> affected_clip_ids; std::int64_t duration_before_us{}; std::int64_t duration_after_us{}; std::string diagnostic; };
struct CompoundSequence { std::string id; TimelineProjectModel project; std::int64_t duration_us{}; };

class ProfessionalTimelineEditor {
 public:
  explicit ProfessionalTimelineEditor(TimelineProjectModel project = {}, std::size_t history_limit = 200);
  [[nodiscard]] const TimelineProjectModel& project() const noexcept;
  [[nodiscard]] const TimelineSelection& selection() const noexcept;
  [[nodiscard]] const std::vector<TimelineMarker>& markers() const noexcept;
  [[nodiscard]] std::int64_t duration_us() const noexcept;
  [[nodiscard]] bool validate() const noexcept;
  bool select_clip(const std::string& clip_id, bool additive = false);
  void clear_selection() noexcept;
  bool add_marker(TimelineMarker marker);
  bool remove_marker(const std::string& marker_id);
  TimelineEditResult insert_clip(const std::string& track_id, TimelineClipModel clip, TimelineInsertMode mode);
  TimelineEditResult lift(const std::string& clip_id, bool include_linked = true);
  TimelineEditResult ripple_delete(const std::string& clip_id, bool include_linked = true);
  TimelineEditResult close_gap(const std::string& track_id, std::int64_t gap_start_us, std::int64_t gap_end_us);
  TimelineEditResult roll_edit(const std::string& left_clip_id, const std::string& right_clip_id, std::int64_t cut_us);
  TimelineEditResult slip_edit(const std::string& clip_id, std::int64_t source_delta_us);
  TimelineEditResult slide_edit(const std::string& clip_id, std::int64_t timeline_delta_us);
  std::optional<CompoundSequence> make_compound(const std::vector<std::string>& clip_ids, const std::string& compound_id, const std::string& destination_track_id);
  bool break_apart_compound(const CompoundSequence& sequence, const std::string& compound_clip_id, const std::string& destination_track_id);
  bool undo();
  bool redo();
  void begin_transaction();
  bool commit_transaction();
  void rollback_transaction();
 private:
  TimelineProjectModel project_;
  TimelineSelection selection_;
  std::vector<TimelineMarker> markers_;
  TimelineHistory history_;
  std::optional<TimelineProjectModel> transaction_base_;
  [[nodiscard]] TimelineTrackModel* find_track(const std::string& id) noexcept;
  [[nodiscard]] TimelineClipModel* find_clip(const std::string& id) noexcept;
  [[nodiscard]] const TimelineClipModel* find_clip(const std::string& id) const noexcept;
  [[nodiscard]] std::vector<std::string> linked_ids(const TimelineClipModel& clip) const;
  [[nodiscard]] std::int64_t compute_duration() const noexcept;
  [[nodiscard]] bool has_overlap(const TimelineTrackModel& track, const std::string& excluding = {}) const noexcept;
  void record_history();
};

}  // namespace digitor
