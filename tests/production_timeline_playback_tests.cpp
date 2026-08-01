#include "digitor/production_timeline_playback.hpp"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

using namespace digitor;

namespace {

std::shared_ptr<const ProductionTimelineSnapshot> snapshot(std::uint64_t revision) {
    auto value = std::make_shared<ProductionTimelineSnapshot>();
    value->revision = revision;
    value->duration_us = 5'000'000;
    value->audio_sample_rate = 48'000;
    ProductionTimelineTrack video;
    video.id = 1;
    video.kind = ProductionTrackKind::video;
    video.order = 0;
    video.clips.push_back({11, "video-a", 0, 5'000'000, 0, 5'000'000,
                           1.0, false, true, 0, 0});
    ProductionTimelineTrack audio;
    audio.id = 2;
    audio.kind = ProductionTrackKind::audio;
    audio.order = 0;
    audio.clips.push_back({12, "audio-a", 0, 5'000'000, 0, 5'000'000,
                           1.0, false, true, 0, 0});
    value->tracks = {video, audio};
    return value;
}

void test_preview_export_identity() {
    auto publisher = std::make_shared<ProductionTimelinePublisher>();
    std::string diagnostic;
    assert(publisher->publish(snapshot(1), diagnostic));
    ProductionTimelinePlayback playback(publisher);
    const auto preview = playback.build_preview_plan(1'000'000, 20'000);
    const auto export_plan = playback.build_export_plan(1'000'000, 20'000);
    assert(preview.status == TimelineEvaluationStatus::ok);
    assert(preview.revision == export_plan.revision);
    assert(preview.decode_requests.size() == export_plan.decode_requests.size());
    assert(preview.render_layers.size() == export_plan.render_layers.size());
    assert(preview.audio_inputs.size() == export_plan.audio_inputs.size());
    assert(preview.decode_requests[0].source_timestamp_us ==
           export_plan.decode_requests[0].source_timestamp_us);
}

void test_async_request_cache_and_revision_invalidation() {
    auto publisher = std::make_shared<ProductionTimelinePublisher>();
    std::string diagnostic;
    assert(publisher->publish(snapshot(1), diagnostic));
    ProductionTimelinePlayback playback(publisher, 8);
    std::mutex mutex;
    std::condition_variable cv;
    int completions = 0;
    TimelinePlaybackResult last;
    playback.set_callbacks([&](const TimelinePlaybackResult& result) {
        std::lock_guard<std::mutex> lock(mutex);
        last = result;
        ++completions;
        cv.notify_all();
    });
    playback.start();
    playback.request(500'000, 10'000);
    {
        std::unique_lock<std::mutex> lock(mutex);
        assert(cv.wait_for(lock, std::chrono::seconds(3), [&] { return completions == 1; }));
    }
    assert(last.request.revision == 1);
    playback.request(500'000, 10'000);
    {
        std::unique_lock<std::mutex> lock(mutex);
        assert(cv.wait_for(lock, std::chrono::seconds(3), [&] { return completions == 2; }));
    }
    assert(last.from_plan_cache);
    assert(publisher->publish(snapshot(2), diagnostic));
    playback.synchronize_revision();
    assert(playback.active_revision() == 2);
    assert(playback.telemetry().revision_invalidations == 1);
    playback.request(500'000, 10'000);
    {
        std::unique_lock<std::mutex> lock(mutex);
        assert(cv.wait_for(lock, std::chrono::seconds(3), [&] { return completions == 3; }));
    }
    assert(last.request.revision == 2);
    assert(!last.from_plan_cache);
    playback.stop();
}

void test_seek_and_latest_request_wins() {
    auto publisher = std::make_shared<ProductionTimelinePublisher>();
    std::string diagnostic;
    assert(publisher->publish(snapshot(7), diagnostic));
    ProductionTimelinePlayback playback(publisher);
    std::mutex mutex;
    std::condition_variable cv;
    std::uint64_t delivered_sequence = 0;
    std::int64_t delivered_timestamp = -1;
    playback.set_callbacks([&](const TimelinePlaybackResult& result) {
        std::lock_guard<std::mutex> lock(mutex);
        delivered_sequence = result.request.sequence;
        delivered_timestamp = result.request.timestamp_us;
        cv.notify_all();
    });
    playback.start();
    playback.pause();
    const auto first = playback.request(100'000);
    const auto second = playback.request(200'000);
    assert(second > first);
    playback.seek(200'000);
    const auto third = playback.request(300'000);
    playback.resume();
    {
        std::unique_lock<std::mutex> lock(mutex);
        assert(cv.wait_for(lock, std::chrono::seconds(3), [&] {
            return delivered_sequence == third;
        }));
    }
    assert(delivered_timestamp == 300'000);
    assert(playback.telemetry().seek_invalidations == 1);
    playback.stop();
}

} // namespace

int main() {
    test_preview_export_identity();
    test_async_request_cache_and_revision_invalidation();
    test_seek_and_latest_request_wins();
    return 0;
}
