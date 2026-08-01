#pragma once

#include "digitor/production_timeline.hpp"
#include "digitor/timeline.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace digitor {

struct TimelineCompileResult {
    TimelineEvaluationStatus status{TimelineEvaluationStatus::ok};
    std::string diagnostic;
    std::shared_ptr<const ProductionTimelineSnapshot> snapshot;
};

struct TimelineDecodeRequest {
    std::uint64_t clip_id{};
    std::string source_id;
    std::int64_t source_timestamp_us{};
    bool audio{};
};

struct TimelineRenderLayer {
    std::uint64_t clip_id{};
    std::int32_t compositing_order{};
    double opacity{1.0};
};

struct TimelineAudioMixInput {
    std::uint64_t clip_id{};
    std::string source_id;
    std::int64_t source_start_us{};
    std::int64_t source_end_us{};
    std::int64_t destination_start_sample{};
    std::int64_t destination_sample_count{};
};

struct TimelineExecutionPlan {
    TimelineEvaluationStatus status{TimelineEvaluationStatus::ok};
    std::string diagnostic;
    std::uint64_t revision{};
    std::int64_t timestamp_us{};
    std::vector<TimelineDecodeRequest> decode_requests;
    std::vector<TimelineRenderLayer> render_layers;
    std::vector<TimelineAudioMixInput> audio_inputs;
};

class ProductionTimelinePublisher {
public:
    [[nodiscard]] bool publish(std::shared_ptr<const ProductionTimelineSnapshot> snapshot,
                               std::string& diagnostic) noexcept;
    [[nodiscard]] std::shared_ptr<const ProductionTimelineSnapshot> acquire() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const ProductionTimelineSnapshot> current_;
};

[[nodiscard]] TimelineCompileResult compile_timeline_snapshot(
    const Timeline& timeline,
    std::uint64_t revision,
    std::uint32_t audio_sample_rate = 48000) noexcept;

[[nodiscard]] TimelineExecutionPlan build_timeline_execution_plan(
    const ProductionTimelineSnapshot& timeline,
    std::int64_t timestamp_us,
    std::int64_t audio_window_duration_us = 0) noexcept;

} // namespace digitor
