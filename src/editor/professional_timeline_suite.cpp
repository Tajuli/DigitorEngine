#include "digitor/professional_timeline_suite.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace digitor {
namespace {
double clamp01(double v) noexcept { return std::clamp(v, 0.0, 1.0); }
double smoothstep(double t) noexcept { t = clamp01(t); return t * t * (3.0 - 2.0 * t); }
bool active(const TimelineClipModel& clip, std::int64_t t) noexcept {
  return clip.visible && t >= clip.start_us && t < clip.start_us + clip.duration_us;
}
}

bool AutomationCurve::set_keyframe(TimelineKeyframe keyframe) {
  if (!std::isfinite(keyframe.value) || keyframe.time_us < 0) return false;
  const auto it = std::lower_bound(keyframes_.begin(), keyframes_.end(), keyframe.time_us,
      [](const TimelineKeyframe& item, std::int64_t time) { return item.time_us < time; });
  if (it != keyframes_.end() && it->time_us == keyframe.time_us) *it = keyframe;
  else keyframes_.insert(it, keyframe);
  return true;
}
bool AutomationCurve::remove_keyframe(std::int64_t time_us) noexcept {
  const auto old = keyframes_.size();
  std::erase_if(keyframes_, [time_us](const auto& k) { return k.time_us == time_us; });
  return old != keyframes_.size();
}
double AutomationCurve::evaluate(std::int64_t time_us, double fallback) const noexcept {
  if (keyframes_.empty()) return fallback;
  if (time_us <= keyframes_.front().time_us) return keyframes_.front().value;
  if (time_us >= keyframes_.back().time_us) return keyframes_.back().value;
  const auto right = std::upper_bound(keyframes_.begin(), keyframes_.end(), time_us,
      [](std::int64_t time, const TimelineKeyframe& item) { return time < item.time_us; });
  const auto& b = *right;
  const auto& a = *(right - 1);
  if (a.interpolation == KeyframeInterpolation::hold) return a.value;
  const auto span = static_cast<double>(b.time_us - a.time_us);
  auto t = span <= 0.0 ? 0.0 : static_cast<double>(time_us - a.time_us) / span;
  if (a.interpolation == KeyframeInterpolation::smooth) t = smoothstep(t);
  return a.value + (b.value - a.value) * t;
}
const std::vector<TimelineKeyframe>& AutomationCurve::keyframes() const noexcept { return keyframes_; }

TransitionSample evaluate_transition(const TransitionSpec& transition, std::int64_t timeline_us) noexcept {
  if (transition.type == TransitionType::none || transition.duration_us <= 0) return {};
  const auto p = clamp01(static_cast<double>(timeline_us - transition.start_us) /
                         static_cast<double>(transition.duration_us));
  const auto shaped = smoothstep(p);
  if (transition.type == TransitionType::dip_to_black) {
    const auto brightness = p < 0.5 ? 1.0 - p * 2.0 : (p - 0.5) * 2.0;
    return {brightness, brightness, p};
  }
  return {1.0 - shaped, shaped, p};
}

AudioMixResult mix_audio_stereo(const std::vector<AudioMixInput>& inputs, std::size_t frames) {
  AudioMixResult out;
  out.interleaved_stereo.assign(frames * 2, 0.0F);
  for (const auto& input : inputs) {
    if (!input.interleaved_stereo || input.muted) continue;
    const auto count = std::min(frames, input.frames);
    const auto pan = std::clamp(input.pan, -1.0, 1.0);
    const auto left_gain = input.gain * (pan <= 0.0 ? 1.0 : 1.0 - pan);
    const auto right_gain = input.gain * (pan >= 0.0 ? 1.0 : 1.0 + pan);
    for (std::size_t i = 0; i < count; ++i) {
      out.interleaved_stereo[i * 2] += static_cast<float>(input.interleaved_stereo[i * 2] * left_gain);
      out.interleaved_stereo[i * 2 + 1] += static_cast<float>(input.interleaved_stereo[i * 2 + 1] * right_gain);
    }
  }
  for (auto& sample : out.interleaved_stereo) {
    if (sample < -1.0F || sample > 1.0F) out.clipped = true;
    sample = std::clamp(sample, -1.0F, 1.0F);
  }
  return out;
}

RenderPlan build_render_plan(const TimelineProjectModel& project, std::int64_t timeline_us) {
  RenderPlan plan;
  plan.timeline_us = std::max<std::int64_t>(timeline_us, 0);
  for (std::size_t ti = 0; ti < project.tracks.size(); ++ti) {
    const auto& track = project.tracks[ti];
    if (track.hidden) continue;
    for (const auto& clip : track.clips) {
      if (!active(clip, plan.timeline_us)) continue;
      if (track.type == TimelineTrackType::video) {
        plan.video_layers.push_back({clip.id, ti, 1.0, 0.0, false});
      } else if (!track.muted && !clip.muted) {
        plan.audio_layers.push_back({clip.id, ti, 0.0, clip.volume, true});
      }
    }
  }
  std::stable_sort(plan.video_layers.begin(), plan.video_layers.end(),
      [](const auto& a, const auto& b) { return a.track_index < b.track_index; });
  return plan;
}

std::size_t RenderCacheKeyHash::operator()(const RenderCacheKey& key) const noexcept {
  auto h = std::hash<std::string>{}(key.clip_id);
  const auto mix = [&h](std::size_t v) { h ^= v + 0x9e3779b9U + (h << 6U) + (h >> 2U); };
  mix(std::hash<std::int64_t>{}(key.source_time_us));
  mix(std::hash<std::uint64_t>{}(key.grade_revision));
  mix(std::hash<std::uint32_t>{}(key.width));
  mix(std::hash<std::uint32_t>{}(key.height));
  return h;
}
TimelineFrameCache::TimelineFrameCache(std::size_t capacity_bytes) : capacity_bytes_(capacity_bytes) {}
void TimelineFrameCache::put(RenderCacheKey key, std::vector<std::uint8_t> bytes) {
  if (capacity_bytes_ == 0 || bytes.size() > capacity_bytes_) return;
  if (const auto it = entries_.find(key); it != entries_.end()) {
    size_bytes_ -= it->second.bytes.size();
    entries_.erase(it);
  }
  size_bytes_ += bytes.size();
  entries_.emplace(std::move(key), Entry{std::move(bytes), ++clock_});
  evict_if_needed();
}
std::optional<std::vector<std::uint8_t>> TimelineFrameCache::get(const RenderCacheKey& key) {
  const auto it = entries_.find(key);
  if (it == entries_.end()) return std::nullopt;
  it->second.stamp = ++clock_;
  return it->second.bytes;
}
void TimelineFrameCache::invalidate_clip(const std::string& clip_id) {
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->first.clip_id == clip_id) { size_bytes_ -= it->second.bytes.size(); it = entries_.erase(it); }
    else ++it;
  }
}
void TimelineFrameCache::clear() noexcept { entries_.clear(); size_bytes_ = 0; }
std::size_t TimelineFrameCache::size_bytes() const noexcept { return size_bytes_; }
void TimelineFrameCache::evict_if_needed() {
  while (size_bytes_ > capacity_bytes_ && !entries_.empty()) {
    auto victim = std::min_element(entries_.begin(), entries_.end(),
        [](const auto& a, const auto& b) { return a.second.stamp < b.second.stamp; });
    size_bytes_ -= victim->second.bytes.size();
    entries_.erase(victim);
  }
}

void RenderJobQueue::push(RenderJob job) {
  jobs_.push_back(job);
  std::stable_sort(jobs_.begin(), jobs_.end(), [](const auto& a, const auto& b) {
    if (a.priority != b.priority) return a.priority > b.priority;
    if (a.generation != b.generation) return a.generation > b.generation;
    return a.id < b.id;
  });
}
std::optional<RenderJob> RenderJobQueue::pop() {
  if (jobs_.empty()) return std::nullopt;
  auto job = jobs_.front();
  jobs_.pop_front();
  return job;
}
void RenderJobQueue::cancel_generation(std::uint64_t generation) {
  std::erase_if(jobs_, [generation](const auto& job) { return job.generation == generation; });
}
std::size_t RenderJobQueue::size() const noexcept { return jobs_.size(); }

TimelineHistory::TimelineHistory(std::size_t limit) : limit_(std::max<std::size_t>(limit, 1)) {}
void TimelineHistory::record(TimelineProjectModel project) {
  undo_.push_back(std::move(project));
  while (undo_.size() > limit_) undo_.pop_front();
  redo_.clear();
}
std::optional<TimelineProjectModel> TimelineHistory::undo(const TimelineProjectModel& current) {
  if (undo_.empty()) return std::nullopt;
  redo_.push_back(current);
  auto result = std::move(undo_.back());
  undo_.pop_back();
  return result;
}
std::optional<TimelineProjectModel> TimelineHistory::redo(const TimelineProjectModel& current) {
  if (redo_.empty()) return std::nullopt;
  undo_.push_back(current);
  auto result = std::move(redo_.back());
  redo_.pop_back();
  return result;
}
void TimelineHistory::clear() noexcept { undo_.clear(); redo_.clear(); }

std::string serialize_timeline_project(const TimelineProjectModel& project) {
  std::ostringstream out;
  out << "DIGITOR_TIMELINE 1\n" << project.fps << ' ' << project.tracks.size() << '\n';
  for (const auto& track : project.tracks) {
    out << "T " << std::quoted(track.id) << ' ' << std::quoted(track.name) << ' '
        << static_cast<int>(track.type) << ' ' << track.locked << ' ' << track.hidden << ' '
        << track.muted << ' ' << track.clips.size() << '\n';
    for (const auto& clip : track.clips) {
      out << "C " << std::quoted(clip.id) << ' ' << static_cast<int>(clip.type) << ' '
          << clip.start_us << ' ' << clip.duration_us << ' ' << clip.source_start_us << ' '
          << (clip.source_duration_us ? *clip.source_duration_us : -1) << ' '
          << std::quoted(clip.link_group_id) << ' ' << std::quoted(clip.source_media_group_id) << ' '
          << clip.embedded_audio << ' ' << clip.visible << ' ' << clip.locked << ' '
          << std::setprecision(17) << clip.volume << ' ' << clip.muted << '\n';
    }
  }
  return out.str();
}

std::optional<TimelineProjectModel> deserialize_timeline_project(const std::string& text) {
  std::istringstream in(text);
  std::string magic;
  int version = 0;
  if (!(in >> magic >> version) || magic != "DIGITOR_TIMELINE" || version != 1) return std::nullopt;
  TimelineProjectModel project;
  std::size_t track_count = 0;
  if (!(in >> project.fps >> track_count) || project.fps <= 0 || track_count > 10000) return std::nullopt;
  for (std::size_t ti = 0; ti < track_count; ++ti) {
    char tag = 0; int type = 0; std::size_t clip_count = 0;
    TimelineTrackModel track;
    if (!(in >> tag >> std::quoted(track.id) >> std::quoted(track.name) >> type >> track.locked >> track.hidden >> track.muted >> clip_count) || tag != 'T' || type < 0 || type > 1 || clip_count > 100000) return std::nullopt;
    track.type = static_cast<TimelineTrackType>(type);
    for (std::size_t ci = 0; ci < clip_count; ++ci) {
      TimelineClipModel clip; int clip_type = 0; std::int64_t source_duration = -1;
      if (!(in >> tag >> std::quoted(clip.id) >> clip_type >> clip.start_us >> clip.duration_us >> clip.source_start_us >> source_duration >> std::quoted(clip.link_group_id) >> std::quoted(clip.source_media_group_id) >> clip.embedded_audio >> clip.visible >> clip.locked >> clip.volume >> clip.muted) || tag != 'C' || clip_type < 0 || clip_type > 4) return std::nullopt;
      clip.type = static_cast<TimelineClipType>(clip_type);
      if (source_duration >= 0) clip.source_duration_us = source_duration;
      track.clips.push_back(std::move(clip));
    }
    project.tracks.push_back(std::move(track));
  }
  MultitrackTimeline validation(project);
  if (!validation.validate()) return std::nullopt;
  return project;
}

}  // namespace digitor
