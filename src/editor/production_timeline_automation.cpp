#include "digitor/production_timeline_automation.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace digitor {
namespace {

const ProductionTimelineClip* find_clip(
    const ProductionTimelineSnapshot& timeline,
    std::uint64_t clip_id) noexcept {
    for (const auto& track : timeline.tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id == clip_id) return &clip;
        }
    }
    return nullptr;
}

bool active_clip_in_plan(const TimelineExecutionPlan& plan,
                         std::uint64_t clip_id) noexcept {
    return std::any_of(plan.render_layers.begin(), plan.render_layers.end(),
        [clip_id](const TimelineRenderLayer& layer) { return layer.clip_id == clip_id; });
}

double interpolate_value(const AutomationCurve& curve,
                         std::int64_t local_timestamp_us) noexcept {
    if (curve.keyframes.empty()) return curve.default_value;
    const auto upper = std::lower_bound(
        curve.keyframes.begin(), curve.keyframes.end(), local_timestamp_us,
        [](const AutomationKeyframe& keyframe, std::int64_t timestamp) {
            return keyframe.timestamp_us < timestamp;
        });
    if (upper == curve.keyframes.begin()) return upper->value;
    if (upper == curve.keyframes.end()) return curve.keyframes.back().value;
    if (upper->timestamp_us == local_timestamp_us) return upper->value;

    const auto& left = *(upper - 1);
    const auto& right = *upper;
    if (left.interpolation == AutomationInterpolation::hold) return left.value;
    const auto span = right.timestamp_us - left.timestamp_us;
    if (span <= 0) return right.value;
    double t = static_cast<double>(local_timestamp_us - left.timestamp_us) /
               static_cast<double>(span);
    t = std::clamp(t, 0.0, 1.0);
    if (left.interpolation == AutomationInterpolation::smoothstep) {
        t = t * t * (3.0 - 2.0 * t);
    }
    return left.value + (right.value - left.value) * t;
}

} // namespace

TimelineEvaluationStatus validate_timeline_automation(
    const ProductionTimelineSnapshot& timeline,
    const std::vector<ClipAutomation>& automation,
    const std::vector<ProductionTransition>& transitions,
    std::string& diagnostic) noexcept {
    try {
        std::unordered_set<std::uint64_t> automation_clip_ids;
        for (const auto& binding : automation) {
            const auto* clip = find_clip(timeline, binding.clip_id);
            if (!clip || !automation_clip_ids.insert(binding.clip_id).second) {
                diagnostic = "automation clip identifiers must exist and be unique";
                return TimelineEvaluationStatus::invalid_timeline;
            }
            std::unordered_set<std::string> parameters;
            for (const auto& curve : binding.curves) {
                if (curve.parameter.empty() || !std::isfinite(curve.default_value) ||
                    !parameters.insert(curve.parameter).second) {
                    diagnostic = "automation parameters must be finite, named and unique per clip";
                    return TimelineEvaluationStatus::invalid_timeline;
                }
                std::int64_t previous = -1;
                for (const auto& keyframe : curve.keyframes) {
                    if (keyframe.timestamp_us < 0 ||
                        keyframe.timestamp_us > clip->timeline_duration_us ||
                        keyframe.timestamp_us <= previous ||
                        !std::isfinite(keyframe.value)) {
                        diagnostic = "automation keyframes must be finite, ordered and within the clip";
                        return TimelineEvaluationStatus::invalid_timeline;
                    }
                    previous = keyframe.timestamp_us;
                }
            }
        }

        std::unordered_set<std::uint64_t> transition_ids;
        for (const auto& transition : transitions) {
            const auto* outgoing = find_clip(timeline, transition.outgoing_clip_id);
            const auto* incoming = find_clip(timeline, transition.incoming_clip_id);
            if (transition.id == 0 || !transition_ids.insert(transition.id).second ||
                transition.kind == ProductionTransitionKind::none || !outgoing || !incoming ||
                outgoing->id == incoming->id || transition.timeline_start_us < 0 ||
                transition.duration_us <= 0 ||
                transition.timeline_start_us + transition.duration_us > timeline.duration_us) {
                diagnostic = "transition identity, clips or timing is invalid";
                return TimelineEvaluationStatus::invalid_timeline;
            }
            if (!std::isfinite(transition.dip_red) || !std::isfinite(transition.dip_green) ||
                !std::isfinite(transition.dip_blue) || !std::isfinite(transition.dip_alpha)) {
                diagnostic = "transition color must be finite";
                return TimelineEvaluationStatus::invalid_timeline;
            }
        }
        diagnostic = "timeline automation valid";
        return TimelineEvaluationStatus::ok;
    } catch (...) {
        diagnostic = "timeline automation validation failed with an internal exception";
        return TimelineEvaluationStatus::invalid_timeline;
    }
}

TimelineAutomationState evaluate_timeline_automation(
    const ProductionTimelineSnapshot& timeline,
    const TimelineExecutionPlan& plan,
    const std::vector<ClipAutomation>& automation,
    const std::vector<ProductionTransition>& transitions,
    std::int64_t timestamp_us) noexcept {
    TimelineAutomationState result;
    result.revision = timeline.revision;
    result.timestamp_us = timestamp_us;
    std::string diagnostic;
    result.status = validate_timeline_automation(timeline, automation, transitions, diagnostic);
    if (result.status != TimelineEvaluationStatus::ok || plan.revision != timeline.revision ||
        plan.status != TimelineEvaluationStatus::ok || timestamp_us < 0 ||
        timestamp_us >= timeline.duration_us) {
        if (result.status == TimelineEvaluationStatus::ok) {
            result.status = TimelineEvaluationStatus::invalid_timeline;
            diagnostic = "automation evaluation requires a matching valid execution plan";
        }
        result.diagnostic = std::move(diagnostic);
        return result;
    }

    for (const auto& binding : automation) {
        if (!active_clip_in_plan(plan, binding.clip_id)) continue;
        const auto* clip = find_clip(timeline, binding.clip_id);
        const auto local = timestamp_us - clip->timeline_start_us;
        if (local < 0 || local >= clip->timeline_duration_us) continue;
        for (const auto& curve : binding.curves) {
            result.parameters.push_back(
                {binding.clip_id, curve.parameter, interpolate_value(curve, local)});
        }
    }

    for (const auto& transition : transitions) {
        if (timestamp_us < transition.timeline_start_us ||
            timestamp_us >= transition.timeline_start_us + transition.duration_us) {
            continue;
        }
        if (!active_clip_in_plan(plan, transition.outgoing_clip_id) &&
            !active_clip_in_plan(plan, transition.incoming_clip_id)) {
            continue;
        }
        const auto elapsed = timestamp_us - transition.timeline_start_us;
        const double progress = std::clamp(
            static_cast<double>(elapsed) / static_cast<double>(transition.duration_us),
            0.0, 1.0);
        TransitionExecutionCommand command;
        command.transition_id = transition.id;
        command.kind = transition.kind;
        command.outgoing_clip_id = transition.outgoing_clip_id;
        command.incoming_clip_id = transition.incoming_clip_id;
        command.progress = progress;
        command.outgoing_weight = 1.0 - progress;
        command.incoming_weight = progress;
        command.dip_red = transition.dip_red;
        command.dip_green = transition.dip_green;
        command.dip_blue = transition.dip_blue;
        command.dip_alpha = transition.dip_alpha;
        if (transition.kind == ProductionTransitionKind::dip_to_color) {
            const double half = progress < 0.5 ? progress * 2.0 : (1.0 - progress) * 2.0;
            command.outgoing_weight = progress < 0.5 ? 1.0 - half : 0.0;
            command.incoming_weight = progress < 0.5 ? 0.0 : 1.0 - half;
        } else if (transition.kind == ProductionTransitionKind::fade) {
            command.incoming_weight = progress;
            command.outgoing_weight = 1.0 - progress;
        }
        result.transitions.push_back(command);
    }

    std::stable_sort(result.parameters.begin(), result.parameters.end(),
        [](const EvaluatedParameter& a, const EvaluatedParameter& b) {
            if (a.clip_id != b.clip_id) return a.clip_id < b.clip_id;
            return a.parameter < b.parameter;
        });
    std::stable_sort(result.transitions.begin(), result.transitions.end(),
        [](const TransitionExecutionCommand& a, const TransitionExecutionCommand& b) {
            return a.transition_id < b.transition_id;
        });
    result.status = TimelineEvaluationStatus::ok;
    result.diagnostic = "timeline automation evaluated";
    return result;
}

} // namespace digitor
