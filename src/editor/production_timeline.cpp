#include "digitor/production_timeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace digitor {
namespace {

bool checked_add(std::int64_t a, std::int64_t b, std::int64_t& out) noexcept {
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) {
        return false;
    }
    out = a + b;
    return true;
}

bool active_at(const ProductionTimelineClip& clip, std::int64_t timestamp_us) noexcept {
    std::int64_t end{};
    return checked_add(clip.timeline_start_us, clip.timeline_duration_us, end) &&
           timestamp_us >= clip.timeline_start_us && timestamp_us < end;
}

std::int64_t map_source_time(const ProductionTimelineClip& clip,
                             std::int64_t timeline_timestamp_us) noexcept {
    const auto local = timeline_timestamp_us - clip.timeline_start_us;
    const auto scaled = static_cast<std::int64_t>(
        std::floor(static_cast<long double>(local) * clip.playback_rate));
    if (!clip.reverse) {
        return clip.source_in_us + scaled;
    }
    const auto last = clip.source_in_us + clip.source_duration_us - 1;
    return last - scaled;
}

double transition_progress(std::int64_t elapsed, std::int64_t duration) noexcept {
    if (duration <= 0) {
        return 1.0;
    }
    return std::clamp(static_cast<double>(elapsed) / static_cast<double>(duration), 0.0, 1.0);
}

std::int64_t us_to_samples(std::int64_t value_us, std::uint32_t sample_rate) noexcept {
    const auto value = static_cast<long double>(value_us) * sample_rate / 1000000.0L;
    return static_cast<std::int64_t>(std::llround(value));
}

} // namespace

TimelineEvaluationStatus validate_production_timeline(
    const ProductionTimelineSnapshot& timeline,
    std::string& diagnostic) noexcept {
    try {
        diagnostic.clear();
        if (timeline.duration_us < 0) {
            diagnostic = "timeline duration must not be negative";
            return TimelineEvaluationStatus::invalid_timeline;
        }
        if (timeline.audio_sample_rate == 0 || timeline.audio_sample_rate > 768000) {
            diagnostic = "audio sample rate is invalid";
            return TimelineEvaluationStatus::invalid_timeline;
        }

        std::unordered_set<std::uint64_t> track_ids;
        std::unordered_set<std::uint64_t> clip_ids;
        for (const auto& track : timeline.tracks) {
            if (track.id == 0 || !track_ids.insert(track.id).second) {
                diagnostic = "track identifiers must be non-zero and unique";
                return TimelineEvaluationStatus::invalid_timeline;
            }
            for (const auto& clip : track.clips) {
                if (clip.id == 0 || !clip_ids.insert(clip.id).second) {
                    diagnostic = "clip identifiers must be non-zero and globally unique";
                    return TimelineEvaluationStatus::invalid_timeline;
                }
                if (clip.source_id.empty()) {
                    diagnostic = "clip source identity must not be empty";
                    return TimelineEvaluationStatus::invalid_timeline;
                }
                if (clip.timeline_start_us < 0 || clip.timeline_duration_us <= 0 ||
                    clip.source_in_us < 0 || clip.source_duration_us <= 0) {
                    diagnostic = "clip timing values are invalid";
                    return TimelineEvaluationStatus::invalid_timeline;
                }
                if (!std::isfinite(clip.playback_rate) || clip.playback_rate <= 0.0) {
                    diagnostic = "clip playback rate must be finite and positive";
                    return TimelineEvaluationStatus::invalid_timeline;
                }
                std::int64_t timeline_end{};
                if (!checked_add(clip.timeline_start_us, clip.timeline_duration_us, timeline_end) ||
                    timeline_end > timeline.duration_us) {
                    diagnostic = "clip exceeds timeline duration";
                    return TimelineEvaluationStatus::invalid_timeline;
                }
                const auto required_source = static_cast<long double>(clip.timeline_duration_us) *
                                             clip.playback_rate;
                if (required_source > static_cast<long double>(clip.source_duration_us)) {
                    diagnostic = "clip source range is shorter than its mapped timeline range";
                    return TimelineEvaluationStatus::invalid_timeline;
                }
                if (clip.transition_in_us < 0 || clip.transition_out_us < 0 ||
                    clip.transition_in_us > clip.timeline_duration_us ||
                    clip.transition_out_us > clip.timeline_duration_us ||
                    clip.transition_in_us + clip.transition_out_us > clip.timeline_duration_us) {
                    diagnostic = "clip transition ranges are invalid";
                    return TimelineEvaluationStatus::invalid_timeline;
                }
            }
        }
        diagnostic = "production timeline valid";
        return TimelineEvaluationStatus::ok;
    } catch (...) {
        diagnostic = "timeline validation failed with an internal exception";
        return TimelineEvaluationStatus::invalid_timeline;
    }
}

ProductionTimelineEvaluation evaluate_production_timeline(
    const ProductionTimelineSnapshot& timeline,
    std::int64_t timestamp_us,
    std::int64_t audio_window_duration_us) noexcept {
    ProductionTimelineEvaluation result;
    result.revision = timeline.revision;
    result.timeline_timestamp_us = timestamp_us;

    std::string diagnostic;
    result.status = validate_production_timeline(timeline, diagnostic);
    if (result.status != TimelineEvaluationStatus::ok) {
        result.diagnostic = std::move(diagnostic);
        return result;
    }
    if (timestamp_us < 0 || timestamp_us >= timeline.duration_us || audio_window_duration_us < 0) {
        result.status = TimelineEvaluationStatus::timestamp_out_of_range;
        result.diagnostic = "timeline timestamp or audio window is out of range";
        return result;
    }

    for (const auto& track : timeline.tracks) {
        if (!track.enabled || (track.kind == ProductionTrackKind::audio && track.muted)) {
            continue;
        }
        for (const auto& clip : track.clips) {
            if (!clip.enabled || !active_at(clip, timestamp_us)) {
                continue;
            }

            if (track.kind == ProductionTrackKind::video) {
                const auto local = timestamp_us - clip.timeline_start_us;
                const auto remaining = clip.timeline_duration_us - local;
                ScheduledVideoClip scheduled;
                scheduled.track_id = track.id;
                scheduled.clip_id = clip.id;
                scheduled.source_id = clip.source_id;
                scheduled.compositing_order = track.order;
                scheduled.source_timestamp_us = map_source_time(clip, timestamp_us);
                scheduled.transition_in_progress = clip.transition_in_us > 0 && local < clip.transition_in_us
                    ? transition_progress(local, clip.transition_in_us) : 1.0;
                scheduled.transition_out_progress = clip.transition_out_us > 0 && remaining <= clip.transition_out_us
                    ? transition_progress(remaining, clip.transition_out_us) : 1.0;
                result.video.push_back(std::move(scheduled));
                continue;
            }

            if (audio_window_duration_us == 0) {
                continue;
            }
            const auto clip_end = clip.timeline_start_us + clip.timeline_duration_us;
            const auto window_end = std::min(timestamp_us + audio_window_duration_us, timeline.duration_us);
            const auto overlap_end = std::min(window_end, clip_end);
            if (overlap_end <= timestamp_us) {
                continue;
            }
            ScheduledAudioClip scheduled;
            scheduled.track_id = track.id;
            scheduled.clip_id = clip.id;
            scheduled.source_id = clip.source_id;
            scheduled.source_start_us = map_source_time(clip, timestamp_us);
            scheduled.source_end_us = map_source_time(clip, overlap_end - 1);
            if (scheduled.source_end_us < scheduled.source_start_us) {
                std::swap(scheduled.source_start_us, scheduled.source_end_us);
            }
            scheduled.destination_start_sample = us_to_samples(timestamp_us, timeline.audio_sample_rate);
            scheduled.destination_sample_count = us_to_samples(overlap_end - timestamp_us,
                                                               timeline.audio_sample_rate);
            result.audio.push_back(std::move(scheduled));
        }
    }

    std::stable_sort(result.video.begin(), result.video.end(),
        [](const ScheduledVideoClip& a, const ScheduledVideoClip& b) {
            if (a.compositing_order != b.compositing_order) {
                return a.compositing_order < b.compositing_order;
            }
            if (a.track_id != b.track_id) {
                return a.track_id < b.track_id;
            }
            return a.clip_id < b.clip_id;
        });
    std::stable_sort(result.audio.begin(), result.audio.end(),
        [](const ScheduledAudioClip& a, const ScheduledAudioClip& b) {
            if (a.track_id != b.track_id) {
                return a.track_id < b.track_id;
            }
            return a.clip_id < b.clip_id;
        });

    result.status = TimelineEvaluationStatus::ok;
    result.diagnostic = "production timeline evaluated";
    return result;
}

} // namespace digitor
