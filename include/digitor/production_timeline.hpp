#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace digitor {

enum class ProductionTrackKind : std::uint8_t { video = 0, audio = 1 };

enum class TimelineEvaluationStatus : std::uint8_t {
    ok = 0,
    invalid_timeline,
    timestamp_out_of_range
};

struct ProductionTimelineClip {
    std::uint64_t id{};
    std::string source_id;
    std::int64_t timeline_start_us{};
    std::int64_t timeline_duration_us{};
    std::int64_t source_in_us{};
    std::int64_t source_duration_us{};
    double playback_rate{1.0};
    bool reverse{};
    bool enabled{true};
    std::int64_t transition_in_us{};
    std::int64_t transition_out_us{};
};

struct ProductionTimelineTrack {
    std::uint64_t id{};
    ProductionTrackKind kind{ProductionTrackKind::video};
    std::int32_t order{};
    bool enabled{true};
    bool muted{};
    std::vector<ProductionTimelineClip> clips;
};

struct ProductionTimelineSnapshot {
    std::uint64_t revision{};
    std::int64_t duration_us{};
    std::uint32_t audio_sample_rate{48000};
    std::vector<ProductionTimelineTrack> tracks;
};

struct ScheduledVideoClip {
    std::uint64_t track_id{};
    std::uint64_t clip_id{};
    std::string source_id;
    std::int32_t compositing_order{};
    std::int64_t source_timestamp_us{};
    double transition_in_progress{};
    double transition_out_progress{};
};

struct ScheduledAudioClip {
    std::uint64_t track_id{};
    std::uint64_t clip_id{};
    std::string source_id;
    std::int64_t source_start_us{};
    std::int64_t source_end_us{};
    std::int64_t destination_start_sample{};
    std::int64_t destination_sample_count{};
};

struct ProductionTimelineEvaluation {
    TimelineEvaluationStatus status{TimelineEvaluationStatus::ok};
    std::string diagnostic;
    std::uint64_t revision{};
    std::int64_t timeline_timestamp_us{};
    std::vector<ScheduledVideoClip> video;
    std::vector<ScheduledAudioClip> audio;
};

[[nodiscard]] TimelineEvaluationStatus validate_production_timeline(
    const ProductionTimelineSnapshot& timeline,
    std::string& diagnostic) noexcept;

[[nodiscard]] ProductionTimelineEvaluation evaluate_production_timeline(
    const ProductionTimelineSnapshot& timeline,
    std::int64_t timestamp_us,
    std::int64_t audio_window_duration_us = 0) noexcept;

} // namespace digitor
