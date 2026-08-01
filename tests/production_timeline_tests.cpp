#include "digitor/production_timeline.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace digitor;

namespace {

ProductionTimelineSnapshot fixture() {
    ProductionTimelineSnapshot timeline;
    timeline.revision = 42;
    timeline.duration_us = 10'000'000;
    timeline.audio_sample_rate = 48'000;

    ProductionTimelineTrack lower;
    lower.id = 1;
    lower.kind = ProductionTrackKind::video;
    lower.order = 0;
    lower.clips.push_back({11, "video-a", 0, 5'000'000, 1'000'000, 5'000'000,
                           1.0, false, true, 500'000, 500'000});

    ProductionTimelineTrack upper;
    upper.id = 2;
    upper.kind = ProductionTrackKind::video;
    upper.order = 10;
    upper.clips.push_back({12, "video-b", 2'000'000, 4'000'000, 500'000, 8'000'000,
                           2.0, true, true, 0, 0});

    ProductionTimelineTrack audio;
    audio.id = 3;
    audio.kind = ProductionTrackKind::audio;
    audio.order = 0;
    audio.clips.push_back({13, "audio-a", 0, 10'000'000, 0, 10'000'000,
                           1.0, false, true, 0, 0});

    timeline.tracks = {upper, audio, lower};
    return timeline;
}

void test_validation() {
    auto timeline = fixture();
    std::string diagnostic;
    assert(validate_production_timeline(timeline, diagnostic) == TimelineEvaluationStatus::ok);

    timeline.tracks[0].clips[0].playback_rate = 0.0;
    assert(validate_production_timeline(timeline, diagnostic) ==
           TimelineEvaluationStatus::invalid_timeline);
}

void test_deterministic_compositing_and_mapping() {
    const auto result = evaluate_production_timeline(fixture(), 2'500'000, 20'000);
    assert(result.status == TimelineEvaluationStatus::ok);
    assert(result.revision == 42);
    assert(result.video.size() == 2);
    assert(result.video[0].track_id == 1);
    assert(result.video[1].track_id == 2);
    assert(result.video[0].source_timestamp_us == 3'500'000);
    assert(result.video[1].source_timestamp_us == 7'499'999);
    assert(result.audio.size() == 1);
    assert(result.audio[0].destination_start_sample == 120'000);
    assert(result.audio[0].destination_sample_count == 960);
}

void test_transition_progress() {
    const auto result = evaluate_production_timeline(fixture(), 250'000);
    assert(result.video.size() == 1);
    assert(std::abs(result.video[0].transition_in_progress - 0.5) < 1e-9);
    assert(result.video[0].transition_out_progress == 1.0);
}

void test_muted_audio_and_half_open_boundary() {
    auto timeline = fixture();
    timeline.tracks[1].muted = true;
    auto result = evaluate_production_timeline(timeline, 4'999'999, 10'000);
    assert(result.video.size() == 2);
    assert(result.audio.empty());

    result = evaluate_production_timeline(timeline, 5'000'000);
    assert(result.video.size() == 1);
    assert(result.video[0].clip_id == 12);
}

void test_out_of_range() {
    const auto result = evaluate_production_timeline(fixture(), 10'000'000);
    assert(result.status == TimelineEvaluationStatus::timestamp_out_of_range);
}

} // namespace

int main() {
    test_validation();
    test_deterministic_compositing_and_mapping();
    test_transition_progress();
    test_muted_audio_and_half_open_boundary();
    test_out_of_range();
    std::cout << "production timeline contract: PASS\n";
    return 0;
}
