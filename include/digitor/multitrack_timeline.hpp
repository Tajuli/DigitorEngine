#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace digitor {

enum class TimelineTrackType { video, audio };
enum class TimelineClipType { video, image, overlay, text, audio };
enum class TrackRemovalPolicy {
  reject_if_not_empty,
  remove_clips,
  remove_clips_and_linked
};

struct TimelineClipModel {
  std::string id;
  TimelineClipType type{TimelineClipType::video};
  std::int64_t start_us{};
  std::int64_t duration_us{};
  std::int64_t source_start_us{};
  std::optional<std::int64_t> source_duration_us;
  std::string link_group_id;
  std::string source_media_group_id;
  bool embedded_audio{};
  bool visible{true};
  bool locked{};
  double volume{1.0};
  bool muted{};
};

struct TimelineTrackModel {
  std::string id;
  std::string name;
  TimelineTrackType type{TimelineTrackType::video};
  bool locked{};
  bool hidden{};
  bool muted{};
  std::vector<TimelineClipModel> clips;
};

struct TimelineProjectModel {
  std::int32_t fps{30};
  std::vector<TimelineTrackModel> tracks;
};

struct TimelineResolvedFrame {
  std::vector<const TimelineClipModel*> video_layers;
  std::vector<const TimelineClipModel*> audio_layers;
};

class MultitrackTimeline {
 public:
  explicit MultitrackTimeline(TimelineProjectModel project = {});

  [[nodiscard]] const TimelineProjectModel& project() const noexcept;
  [[nodiscard]] std::int64_t duration_us() const noexcept;
  [[nodiscard]] bool validate() const noexcept;
  [[nodiscard]] std::int64_t snap(std::int64_t value_us,
                                  std::int64_t threshold_us,
                                  const std::string& excluding_clip_id = {}) const noexcept;

  bool add_track(TimelineTrackModel track);
  bool remove_track(const std::string& track_id,
                    TrackRemovalPolicy policy = TrackRemovalPolicy::reject_if_not_empty);
  bool add_clip(const std::string& track_id, TimelineClipModel clip);
  bool remove_clip(const std::string& clip_id, bool remove_linked = true);
  bool link_clips(const std::string& first_clip_id, const std::string& second_clip_id,
                  const std::string& group_id);
  bool unlink_group(const std::string& group_id);
  bool move_clip(const std::string& clip_id, const std::string& to_track_id,
                 std::int64_t start_us, bool move_linked = true);
  bool trim_clip(const std::string& clip_id, std::int64_t new_start_us,
                 std::int64_t new_end_us, bool trim_linked = true);
  bool split_clip(const std::string& clip_id, std::int64_t position_us,
                  const std::string& second_id, bool split_linked = true);
  bool ripple_move(const std::string& clip_id, std::int64_t start_us);

  [[nodiscard]] TimelineResolvedFrame resolve(std::int64_t position_us) const noexcept;

 private:
  TimelineProjectModel project_;

  [[nodiscard]] TimelineTrackModel* find_track(const std::string& id) noexcept;
  [[nodiscard]] const TimelineTrackModel* find_track(const std::string& id) const noexcept;
  [[nodiscard]] TimelineTrackModel* containing_track(const std::string& clip_id) noexcept;
  [[nodiscard]] const TimelineTrackModel* containing_track(const std::string& clip_id) const noexcept;
  [[nodiscard]] TimelineClipModel* find_clip(const std::string& clip_id) noexcept;
  [[nodiscard]] const TimelineClipModel* find_clip(const std::string& clip_id) const noexcept;
  [[nodiscard]] bool accepts(const TimelineTrackModel& track, const TimelineClipModel& clip) const noexcept;
  [[nodiscard]] bool track_has_overlap(const TimelineTrackModel& track,
                                       const std::string& excluding = {}) const noexcept;
  [[nodiscard]] std::vector<std::string> linked_ids(const TimelineClipModel& clip) const;
};

}  // namespace digitor
