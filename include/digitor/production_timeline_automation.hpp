#pragma once

#include "digitor/production_timeline_runtime.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace digitor {

enum class AutomationInterpolation : std::uint8_t {
    hold = 0,
    linear,
    smoothstep
};

struct AutomationKeyframe {
    std::int64_t timestamp_us{};
    double value{};
    AutomationInterpolation interpolation{AutomationInterpolation::linear};
};

struct AutomationCurve {
    std::string parameter;
    double default_value{};
    std::vector<AutomationKeyframe> keyframes;
};

struct ClipAutomation {
    std::uint64_t clip_id{};
    std::vector<AutomationCurve> curves;
};

enum class ProductionTransitionKind : std::uint8_t {
    none = 0,
    cross_dissolve,
    dip_to_color,
    fade
};

struct ProductionTransition {
    std::uint64_t id{};
    ProductionTransitionKind kind{ProductionTransitionKind::none};
    std::uint64_t outgoing_clip_id{};
    std::uint64_t incoming_clip_id{};
    std::int64_t timeline_start_us{};
    std::int64_t duration_us{};
    double dip_red{};
    double dip_green{};
    double dip_blue{};
    double dip_alpha{1.0};
};

struct EvaluatedParameter {
    std::uint64_t clip_id{};
    std::string parameter;
    double value{};
};

struct TransitionExecutionCommand {
    std::uint64_t transition_id{};
    ProductionTransitionKind kind{ProductionTransitionKind::none};
    std::uint64_t outgoing_clip_id{};
    std::uint64_t incoming_clip_id{};
    double progress{};
    double outgoing_weight{1.0};
    double incoming_weight{};
    double dip_red{};
    double dip_green{};
    double dip_blue{};
    double dip_alpha{1.0};
};

struct TimelineAutomationState {
    TimelineEvaluationStatus status{TimelineEvaluationStatus::ok};
    std::string diagnostic;
    std::uint64_t revision{};
    std::int64_t timestamp_us{};
    std::vector<EvaluatedParameter> parameters;
    std::vector<TransitionExecutionCommand> transitions;
};

[[nodiscard]] TimelineEvaluationStatus validate_timeline_automation(
    const ProductionTimelineSnapshot& timeline,
    const std::vector<ClipAutomation>& automation,
    const std::vector<ProductionTransition>& transitions,
    std::string& diagnostic) noexcept;

[[nodiscard]] TimelineAutomationState evaluate_timeline_automation(
    const ProductionTimelineSnapshot& timeline,
    const TimelineExecutionPlan& plan,
    const std::vector<ClipAutomation>& automation,
    const std::vector<ProductionTransition>& transitions,
    std::int64_t timestamp_us) noexcept;

} // namespace digitor
