#include "digitor/production_timeline_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace digitor {
namespace {

bool frame_to_us(FrameNumber frame, Rational rate, std::int64_t& out) noexcept {
    if (frame < 0 || rate.numerator <= 0 || rate.denominator <= 0) {
        return false;
    }
    const long double value = static_cast<long double>(frame) * 1000000.0L *
                              static_cast<long double>(rate.numerator) /
                              static_cast<long double>(rate.denominator);
    if (value > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    out = static_cast<std::int64_t>(std::llround(value));
    return true;
}

} // namespace

bool ProductionTimelinePublisher::publish(
    std::shared_ptr<const ProductionTimelineSnapshot> snapshot,
    std::string& diagnostic) noexcept {
    if (!snapshot) {
        diagnostic = "timeline snapshot must not be null";
        return false;
    }
    std::string validation;
    if (validate_production_timeline(*snapshot, validation) != TimelineEvaluationStatus::ok) {
        diagnostic = validation;
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_ && snapshot->revision <= current_->revision) {
        diagnostic = "timeline revision must increase monotonically";
        return false;
    }
    current_ = std::move(snapshot);
    diagnostic = "timeline snapshot published";
    return true;
}

std::shared_ptr<const ProductionTimelineSnapshot>
ProductionTimelinePublisher::acquire() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_;
}

std::uint64_t ProductionTimelinePublisher::revision() const noexcept {
    const auto snapshot = acquire();
    return snapshot ? snapshot->revision : 0;
}

TimelineCompileResult compile_timeline_snapshot(
    const Timeline& timeline,
    std::uint64_t revision,
    std::uint32_t audio_sample_rate) noexcept {
    TimelineCompileResult result;
    try {
        if (revision == 0 || audio_sample_rate == 0) {
            result.status = TimelineEvaluationStatus::invalid_timeline;
            result.diagnostic = "revision and audio sample rate must be non-zero";
            return result;
        }

        auto snapshot = std::make_shared<ProductionTimelineSnapshot>();
        snapshot->revision = revision;
        snapshot->audio_sample_rate = audio_sample_rate;
        std::unordered_set<const Timeline*> recursion_guard;

        const auto compile_sequence = [&](const auto& self,
                                          const Timeline& sequence,
                                          std::int64_t parent_start_us,
                                          std::int32_t order_base,
                                          ProductionTimelineSnapshot& output,
                                          std::unordered_set<const Timeline*>& guard) -> bool {
            if (!guard.insert(&sequence).second) {
                result.diagnostic = "nested timeline cycle detected";
                return false;
            }
            for (std::size_t track_index = 0; track_index < sequence.tracks().size(); ++track_index) {
                const auto& source_track = sequence.tracks()[track_index];
                ProductionTimelineTrack track;
                track.id = static_cast<std::uint64_t>(output.tracks.size() + 1);
                track.kind = ProductionTrackKind::video;
                track.order = order_base + static_cast<std::int32_t>(track_index);
                for (const auto& clip : source_track.clips) {
                    std::int64_t start_us{}, duration_us{}, source_in_us{};
                    if (!frame_to_us(clip.start, sequence.frame_rate(), start_us) ||
                        !frame_to_us(clip.duration, sequence.frame_rate(), duration_us) ||
                        !frame_to_us(clip.source_in, sequence.frame_rate(), source_in_us)) {
                        result.diagnostic = "legacy clip timing cannot be represented in microseconds";
                        guard.erase(&sequence);
                        return false;
                    }
                    if (const auto* nested = sequence.nested(clip.id)) {
                        if (!self(self, *nested, parent_start_us + start_us,
                                  track.order * 1000, output, guard)) {
                            guard.erase(&sequence);
                            return false;
                        }
                        continue;
                    }
                    ProductionTimelineClip compiled;
                    compiled.id = clip.id;
                    compiled.source_id = clip.source;
                    compiled.timeline_start_us = parent_start_us + start_us;
                    compiled.timeline_duration_us = duration_us;
                    compiled.source_in_us = source_in_us;
                    compiled.source_duration_us = duration_us;
                    track.clips.push_back(std::move(compiled));
                    output.duration_us = std::max(output.duration_us,
                                                  parent_start_us + start_us + duration_us);
                }
                if (!track.clips.empty()) {
                    output.tracks.push_back(std::move(track));
                }
            }
            guard.erase(&sequence);
            return true;
        };

        if (!compile_sequence(compile_sequence, timeline, 0, 0, *snapshot, recursion_guard)) {
            result.status = TimelineEvaluationStatus::invalid_timeline;
            return result;
        }
        std::string validation;
        result.status = validate_production_timeline(*snapshot, validation);
        result.diagnostic = validation;
        if (result.status == TimelineEvaluationStatus::ok) {
            result.snapshot = std::move(snapshot);
        }
        return result;
    } catch (...) {
        result.status = TimelineEvaluationStatus::invalid_timeline;
        result.diagnostic = "timeline compilation failed with an internal exception";
        return result;
    }
}

TimelineExecutionPlan build_timeline_execution_plan(
    const ProductionTimelineSnapshot& timeline,
    std::int64_t timestamp_us,
    std::int64_t audio_window_duration_us) noexcept {
    TimelineExecutionPlan plan;
    plan.revision = timeline.revision;
    plan.timestamp_us = timestamp_us;
    const auto evaluated = evaluate_production_timeline(
        timeline, timestamp_us, audio_window_duration_us);
    plan.status = evaluated.status;
    plan.diagnostic = evaluated.diagnostic;
    if (evaluated.status != TimelineEvaluationStatus::ok) {
        return plan;
    }

    for (const auto& clip : evaluated.video) {
        plan.decode_requests.push_back({clip.clip_id, clip.source_id,
                                        clip.source_timestamp_us, false});
        plan.render_layers.push_back({clip.clip_id, clip.compositing_order,
                                      clip.transition_in_progress *
                                      clip.transition_out_progress});
    }
    for (const auto& clip : evaluated.audio) {
        plan.decode_requests.push_back({clip.clip_id, clip.source_id,
                                        clip.source_start_us, true});
        plan.audio_inputs.push_back({clip.clip_id, clip.source_id,
                                     clip.source_start_us, clip.source_end_us,
                                     clip.destination_start_sample,
                                     clip.destination_sample_count});
    }
    return plan;
}

} // namespace digitor
