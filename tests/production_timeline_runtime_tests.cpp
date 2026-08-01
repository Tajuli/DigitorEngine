#include "digitor/production_timeline_runtime.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

using namespace digitor;

namespace {

void test_legacy_snapshot_compile_and_plan() {
    Timeline timeline({1, 30});
    const auto track = timeline.add_track("V1");
    timeline.add_clip(track, "clip-a.mp4", 0, 90, 30);

    const auto compiled = compile_timeline_snapshot(timeline, 7, 48000);
    assert(compiled.status == TimelineEvaluationStatus::ok);
    assert(compiled.snapshot);
    assert(compiled.snapshot->revision == 7);
    assert(compiled.snapshot->duration_us == 3'000'000);
    assert(compiled.snapshot->tracks.size() == 1);

    const auto plan = build_timeline_execution_plan(*compiled.snapshot, 1'000'000);
    assert(plan.status == TimelineEvaluationStatus::ok);
    assert(plan.revision == 7);
    assert(plan.decode_requests.size() == 1);
    assert(plan.decode_requests[0].source_timestamp_us == 2'000'000);
    assert(plan.render_layers.size() == 1);
    assert(plan.render_layers[0].opacity == 1.0);
}

void test_nested_flattening() {
    Timeline child({1, 30});
    const auto child_track = child.add_track("nested");
    child.add_clip(child_track, "nested-source.mp4", 0, 30, 0);

    Timeline root({1, 30});
    const auto root_track = root.add_track("root");
    root.add_nested(root_track, child, 60);

    const auto compiled = compile_timeline_snapshot(root, 8);
    assert(compiled.status == TimelineEvaluationStatus::ok);
    assert(compiled.snapshot->duration_us == 3'000'000);
    const auto plan = build_timeline_execution_plan(*compiled.snapshot, 2'500'000);
    assert(plan.status == TimelineEvaluationStatus::ok);
    assert(plan.decode_requests.size() == 1);
    assert(plan.decode_requests[0].source_id == "nested-source.mp4");
    assert(plan.decode_requests[0].source_timestamp_us == 500'000);
}

void test_revision_publisher() {
    ProductionTimelinePublisher publisher;
    std::string diagnostic;

    auto timeline = Timeline({1, 30});
    const auto track = timeline.add_track();
    timeline.add_clip(track, "a.mp4", 0, 30);

    auto first = compile_timeline_snapshot(timeline, 10).snapshot;
    auto second = compile_timeline_snapshot(timeline, 11).snapshot;
    assert(publisher.publish(first, diagnostic));
    assert(publisher.revision() == 10);
    assert(!publisher.publish(first, diagnostic));
    assert(publisher.publish(second, diagnostic));
    const auto acquired = publisher.acquire();
    assert(acquired && acquired->revision == 11);
}

void test_audio_mix_plan() {
    ProductionTimelineSnapshot snapshot;
    snapshot.revision = 12;
    snapshot.duration_us = 2'000'000;
    snapshot.audio_sample_rate = 48'000;
    ProductionTimelineTrack track;
    track.id = 1;
    track.kind = ProductionTrackKind::audio;
    track.clips.push_back({2, "audio.wav", 0, 2'000'000, 0, 2'000'000,
                           1.0, false, true, 0, 0});
    snapshot.tracks.push_back(track);

    const auto plan = build_timeline_execution_plan(snapshot, 500'000, 20'000);
    assert(plan.status == TimelineEvaluationStatus::ok);
    assert(plan.audio_inputs.size() == 1);
    assert(plan.audio_inputs[0].destination_start_sample == 24'000);
    assert(plan.audio_inputs[0].destination_sample_count == 960);
    assert(plan.decode_requests.size() == 1);
    assert(plan.decode_requests[0].audio);
}

} // namespace

int main() {
    test_legacy_snapshot_compile_and_plan();
    test_nested_flattening();
    test_revision_publisher();
    test_audio_mix_plan();
    std::cout << "production timeline runtime integration: PASS\n";
    return 0;
}
