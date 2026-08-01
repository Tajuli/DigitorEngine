#pragma once

#include "digitor/multitrack_timeline.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class KeyframeInterpolation { hold, linear, smooth };
struct TimelineKeyframe { std::int64_t time_us{}; double value{}; KeyframeInterpolation interpolation{KeyframeInterpolation::linear}; };
class AutomationCurve {
 public:
  bool set_keyframe(TimelineKeyframe keyframe);
  bool remove_keyframe(std::int64_t time_us) noexcept;
  [[nodiscard]] double evaluate(std::int64_t time_us, double fallback = 0.0) const noexcept;
  [[nodiscard]] const std::vector<TimelineKeyframe>& keyframes() const noexcept;
 private:
  std::vector<TimelineKeyframe> keyframes_;
};

enum class TransitionType { none, cross_dissolve, dip_to_black, wipe_left, wipe_right };
struct TransitionSpec { TransitionType type{TransitionType::none}; std::int64_t start_us{}; std::int64_t duration_us{}; };
struct TransitionSample { double outgoing_weight{1.0}; double incoming_weight{}; double progress{}; };
[[nodiscard]] TransitionSample evaluate_transition(const TransitionSpec& transition, std::int64_t timeline_us) noexcept;

struct AudioMixInput { const float* interleaved_stereo{}; std::size_t frames{}; double gain{1.0}; double pan{}; bool muted{}; };
struct AudioMixResult { std::vector<float> interleaved_stereo; bool clipped{}; };
[[nodiscard]] AudioMixResult mix_audio_stereo(const std::vector<AudioMixInput>& inputs, std::size_t frames);

struct RenderPlanLayer { std::string clip_id; std::size_t track_index{}; double opacity{1.0}; double volume{1.0}; bool audio{}; };
struct RenderPlan { std::int64_t timeline_us{}; std::vector<RenderPlanLayer> video_layers; std::vector<RenderPlanLayer> audio_layers; };
[[nodiscard]] RenderPlan build_render_plan(const TimelineProjectModel& project, std::int64_t timeline_us);

struct RenderCacheKey {
  std::string clip_id;
  std::int64_t source_time_us{};
  std::uint64_t grade_revision{};
  std::uint32_t width{};
  std::uint32_t height{};
  bool operator==(const RenderCacheKey&) const = default;
};
struct RenderCacheKeyHash { std::size_t operator()(const RenderCacheKey& key) const noexcept; };
class TimelineFrameCache {
 public:
  explicit TimelineFrameCache(std::size_t capacity_bytes);
  void put(RenderCacheKey key, std::vector<std::uint8_t> bytes);
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> get(const RenderCacheKey& key);
  void invalidate_clip(const std::string& clip_id);
  void clear() noexcept;
  [[nodiscard]] std::size_t size_bytes() const noexcept;
 private:
  struct Entry { std::vector<std::uint8_t> bytes; std::uint64_t stamp{}; };
  void evict_if_needed();
  std::size_t capacity_bytes_{};
  std::size_t size_bytes_{};
  std::uint64_t clock_{};
  std::unordered_map<RenderCacheKey, Entry, RenderCacheKeyHash> entries_;
};

enum class RenderJobPriority : std::uint8_t { background = 0, normal = 1, interactive = 2 };
struct RenderJob { std::uint64_t id{}; RenderJobPriority priority{RenderJobPriority::normal}; std::int64_t timeline_us{}; std::uint64_t generation{}; };
class RenderJobQueue {
 public:
  void push(RenderJob job);
  [[nodiscard]] std::optional<RenderJob> pop();
  void cancel_generation(std::uint64_t generation);
  [[nodiscard]] std::size_t size() const noexcept;
 private:
  std::deque<RenderJob> jobs_;
};

class TimelineHistory {
 public:
  explicit TimelineHistory(std::size_t limit = 100);
  void record(TimelineProjectModel project);
  [[nodiscard]] std::optional<TimelineProjectModel> undo(const TimelineProjectModel& current);
  [[nodiscard]] std::optional<TimelineProjectModel> redo(const TimelineProjectModel& current);
  void clear() noexcept;
 private:
  std::size_t limit_{};
  std::deque<TimelineProjectModel> undo_;
  std::deque<TimelineProjectModel> redo_;
};

[[nodiscard]] std::string serialize_timeline_project(const TimelineProjectModel& project);
[[nodiscard]] std::optional<TimelineProjectModel> deserialize_timeline_project(const std::string& text);

}  // namespace digitor
