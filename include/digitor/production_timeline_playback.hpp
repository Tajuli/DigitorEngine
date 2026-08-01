#pragma once

#include "digitor/production_timeline_runtime.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace digitor {

struct TimelinePlaybackRequest {
    std::uint64_t sequence{};
    std::uint64_t epoch{};
    std::uint64_t revision{};
    std::int64_t timestamp_us{};
    std::int64_t audio_window_duration_us{};
};

struct TimelinePlaybackResult {
    TimelinePlaybackRequest request;
    TimelineExecutionPlan plan;
    bool from_plan_cache{};
};

struct TimelinePlaybackTelemetry {
    std::uint64_t requested{};
    std::uint64_t completed{};
    std::uint64_t stale_rejected{};
    std::uint64_t cache_hits{};
    std::uint64_t cache_misses{};
    std::uint64_t revision_invalidations{};
    std::uint64_t seek_invalidations{};
};

class ProductionTimelinePlayback final {
public:
    using CompletionCallback = std::function<void(const TimelinePlaybackResult&)>;
    using ErrorCallback = std::function<void(const TimelinePlaybackRequest&, std::exception_ptr)>;

    explicit ProductionTimelinePlayback(
        std::shared_ptr<ProductionTimelinePublisher> publisher,
        std::size_t plan_cache_capacity = 256);
    ~ProductionTimelinePlayback();

    ProductionTimelinePlayback(const ProductionTimelinePlayback&) = delete;
    ProductionTimelinePlayback& operator=(const ProductionTimelinePlayback&) = delete;

    void start();
    void stop() noexcept;
    void pause() noexcept;
    void resume() noexcept;

    void set_callbacks(CompletionCallback completion, ErrorCallback error = {});

    [[nodiscard]] std::uint64_t request(
        std::int64_t timestamp_us,
        std::int64_t audio_window_duration_us = 0);

    void seek(std::int64_t timestamp_us);
    void synchronize_revision();
    void invalidate_all();

    [[nodiscard]] std::uint64_t epoch() const noexcept;
    [[nodiscard]] std::uint64_t active_revision() const noexcept;
    [[nodiscard]] TimelinePlaybackTelemetry telemetry() const noexcept;

    [[nodiscard]] TimelineExecutionPlan build_preview_plan(
        std::int64_t timestamp_us,
        std::int64_t audio_window_duration_us = 0) const noexcept;

    [[nodiscard]] TimelineExecutionPlan build_export_plan(
        std::int64_t timestamp_us,
        std::int64_t audio_window_duration_us = 0) const noexcept;

private:
    struct PlanCacheKey {
        std::uint64_t revision{};
        std::int64_t timestamp_us{};
        std::int64_t audio_window_duration_us{};
        bool operator==(const PlanCacheKey&) const noexcept = default;
    };

    struct PlanCacheKeyHash {
        std::size_t operator()(const PlanCacheKey& key) const noexcept;
    };

    [[nodiscard]] bool current(const TimelinePlaybackRequest& request) const noexcept;
    [[nodiscard]] TimelineExecutionPlan build_plan(
        const ProductionTimelineSnapshot& snapshot,
        std::int64_t timestamp_us,
        std::int64_t audio_window_duration_us) const noexcept;
    void run() noexcept;
    void clear_cache_locked();

    std::shared_ptr<ProductionTimelinePublisher> publisher_;
    const std::size_t cache_capacity_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<TimelinePlaybackRequest> queue_;
    std::unordered_map<PlanCacheKey, TimelineExecutionPlan, PlanCacheKeyHash> plan_cache_;
    std::deque<PlanCacheKey> cache_order_;
    std::thread worker_;
    CompletionCallback completion_;
    ErrorCallback error_;
    bool stopping_{};
    bool paused_{};

    std::atomic_uint64_t epoch_{1};
    std::atomic_uint64_t latest_sequence_{0};
    std::atomic_uint64_t active_revision_{0};
    std::atomic_uint64_t requested_{0};
    std::atomic_uint64_t completed_{0};
    std::atomic_uint64_t stale_rejected_{0};
    std::atomic_uint64_t cache_hits_{0};
    std::atomic_uint64_t cache_misses_{0};
    std::atomic_uint64_t revision_invalidations_{0};
    std::atomic_uint64_t seek_invalidations_{0};
};

} // namespace digitor
