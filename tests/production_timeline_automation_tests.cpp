#include "digitor/production_timeline_automation.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace digitor;

namespace {

ProductionTimelineSnapshot timeline_fixture() {
    ProductionTimelineSnapshot timeline;
    timeline.revision = 77;
    timeline.duration_us = 4'000'000;
    timeline.audio_sample_rate = 48'000;
    ProductionTimelineTrack track;
    track.id = 1;
    track.kind = ProductionTrackKind::video;
    track.order = 0;
    track.clips.push_back({11, "a", 0, 3'000'000, 0, 3'000'000, 1.0, false, true, 0, 0});
    track.clips.push_back({12, "b", 1'000'000, 3'000'000, 0, 3'000'000, 1.0, false, true, 0, 0});
    timeline.tracks.push_back(track);
    return timeline;
}

ClipAutomation automation_fixture() {
    ClipAutomation automation;
    automation.clip_id = 11;
    AutomationCurve opacity;
    opacity.parameter = "opacity";
    opacity.default_value = 1.0;
    opacity.keyframes = {
        {0, 0.0, AutomationInterpolation::linear},
        {1'000'000, 1.0, AutomationInterpolation::smoothstep},
        {2'000'000, 0.0, AutomationInterpolation::hold}
    };
    automation.curves.push_back(opacity);
    return automation;
}

ProductionTransition transition_fixture() {
    ProductionTransition transition;
    transition.id = 9;
    transition.kind = ProductionTransitionKind::cross_dissolve;
    transition.outgoing_clip_id = 11;
    transition.incoming_clip_id = 12;
    transition.timeline_start_us = 1'000'000;
    transition.duration_us = 1'000'000;
    return transition;
}

void test_linear_and_smooth_interpolation() {
    const auto timeline = timeline_fixture();
    const auto plan = build_timeline_execution_plan(timeline, 500'000, 0);
    const auto state = evaluate_timeline_automation(
        timeline, plan, {automation_fixture()}, {}, 500'000);
    assert(state.status == TimelineEvaluationStatus::ok);
    assert(state.parameters.size() == 1);
    assert(std::abs(state.parameters[0].value - 0.5) < 1e-9);

    const auto plan2 = build_timeline_execution_plan(timeline, 1'500'000, 0);
    const auto state2 = evaluate_timeline_automation(
        timeline, plan2, {automation_fixture()}, {}, 1'500'000);
    assert(std::abs(state2.parameters[0].value - 0.5) < 1e-9);
}

void test_transition_midpoint_and_identity() {
    const auto timeline = timeline_fixture();
    const auto plan = build_timeline_execution_plan(timeline, 1'500'000, 0);
    const auto state = evaluate_timeline_automation(
        timeline, plan, {}, {transition_fixture()}, 1'500'000);
    assert(state.status == TimelineEvaluationStatus::ok);
    assert(state.revision == plan.revision);
    assert(state.transitions.size() == 1);
    assert(std::abs(state.transitions[0].progress - 0.5) < 1e-9);
    assert(std::abs(state.transitions[0].outgoing_weight - 0.5) < 1e-9);
    assert(std::abs(state.transitions[0].incoming_weight - 0.5) < 1e-9);
}

void test_dip_to_color() {
    auto transition = transition_fixture();
    transition.kind = ProductionTransitionKind::dip_to_color;
    transition.dip_red = 0.1;
    transition.dip_green = 0.2;
    transition.dip_blue = 0.3;
    const auto timeline = timeline_fixture();
    const auto plan = build_timeline_execution_plan(timeline, 1'500'000, 0);
    const auto state = evaluate_timeline_automation(
        timeline, plan, {}, {transition}, 1'500'000);
    assert(state.transitions.size() == 1);
    assert(state.transitions[0].outgoing_weight == 0.0);
    assert(state.transitions[0].incoming_weight == 0.0);
    assert(state.transitions[0].dip_green == 0.2);
}

void test_invalid_keyframes_and_revision_mismatch() {
    auto automation = automation_fixture();
    automation.curves[0].keyframes[1].timestamp_us = 0;
    const auto timeline = timeline_fixture();
    std::string diagnostic;
    assert(validate_timeline_automation(timeline, {automation}, {}, diagnostic) ==
           TimelineEvaluationStatus::invalid_timeline);

    auto plan = build_timeline_execution_plan(timeline, 500'000, 0);
    plan.revision = 999;
    const auto state = evaluate_timeline_automation(
        timeline, plan, {automation_fixture()}, {}, 500'000);
    assert(state.status == TimelineEvaluationStatus::invalid_timeline);
}

void test_preview_export_determinism() {
    const auto timeline = timeline_fixture();
    const auto preview = build_timeline_execution_plan(timeline, 1'250'000, 0);
    const auto export_plan = build_timeline_execution_plan(timeline, 1'250'000, 0);
    const auto preview_state = evaluate_timeline_automation(
        timeline, preview, {automation_fixture()}, {transition_fixture()}, 1'250'000);
    const auto export_state = evaluate_timeline_automation(
        timeline, export_plan, {automation_fixture()}, {transition_fixture()}, 1'250'000);
    assert(preview_state.revision == export_state.revision);
    assert(preview_state.parameters.size() == export_state.parameters.size());
    assert(preview_state.transitions.size() == export_state.transitions.size());
    assert(preview_state.parameters[0].value == export_state.parameters[0].value);
    assert(preview_state.transitions[0].progress == export_state.transitions[0].progress);
}

} // namespace

int main() {
    test_linear_and_smooth_interpolation();
    test_transition_midpoint_and_identity();
    test_dip_to_color();
    test_invalid_keyframes_and_revision_mismatch();
    test_preview_export_determinism();
    std::cout << "production timeline automation: PASS\n";
    return 0;
}
