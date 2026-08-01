#include "digitor/production_timeline_playback.hpp"

#include <stdexcept>

namespace digitor {

std::size_t ProductionTimelinePlayback::PlanCacheKeyHash::operator()(
    const PlanCacheKey& key) const noexcept {
    std::size_t seed = std::hash<std::uint64_t>{}(key.revision);
    const auto combine = [](std::size_t& value, std::size_t next) {
        value ^= next + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
    };
    combine(seed, std::hash<std::int64_t>{}(key.timestamp_us));
    combine(seed, std::hash<std::int64_t>{}(key.audio_window_duration_us));
    return seed;
}

ProductionTimelinePlayback::ProductionTimelinePlayback(
    std::shared_ptr<ProductionTimelinePublisher> publisher,
    std::size_t plan_cache_capacity)
    : publisher_(std::move(publisher)), cache_capacity_(plan_cache_capacity) {
    if (!publisher_) {
        throw std::invalid_argument("timeline publisher is required");
    }
    active_revision_.store(publisher_->revision(), std::memory_order_release);
}

ProductionTimelinePlayback::~ProductionTimelinePlayback() { stop(); }

void ProductionTimelinePlayback::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable()) return;
    stopping_ = false;
    worker_ = std::thread([this] { run(); });
}

void ProductionTimelinePlayback::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void ProductionTimelinePlayback::pause() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = true;
}

void ProductionTimelinePlayback::resume() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = false;
    }
    cv_.notify_all();
}

void ProductionTimelinePlayback::set_callbacks(
    CompletionCallback completion, ErrorCallback error) {
    std::lock_guard<std::mutex> lock(mutex_);
    completion_ = std::move(completion);
    error_ = std::move(error);
}

std::uint64_t ProductionTimelinePlayback::request(
    std::int64_t timestamp_us, std::int64_t audio_window_duration_us) {
    if (timestamp_us < 0 || audio_window_duration_us < 0) {
        throw std::invalid_argument("timeline playback timestamps must be non-negative");
    }
    synchronize_revision();
    const auto sequence = latest_sequence_.fetch_add(1, std::memory_order_acq_rel) + 1;
    TimelinePlaybackRequest request_value;
    request_value.sequence = sequence;
    request_value.epoch = epoch_.load(std::memory_order_acquire);
    request_value.revision = active_revision_.load(std::memory_order_acquire);
    request_value.timestamp_us = timestamp_us;
    request_value.audio_window_duration_us = audio_window_duration_us;
    requested_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        queue_.push_back(request_value);
    }
    cv_.notify_all();
    return sequence;
}

void ProductionTimelinePlayback::seek(std::int64_t timestamp_us) {
    if (timestamp_us < 0) throw std::invalid_argument("seek timestamp must be non-negative");
    epoch_.fetch_add(1, std::memory_order_acq_rel);
    latest_sequence_.fetch_add(1, std::memory_order_acq_rel);
    seek_invalidations_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        clear_cache_locked();
    }
    cv_.notify_all();
}

void ProductionTimelinePlayback::synchronize_revision() {
    const auto revision = publisher_->revision();
    const auto previous = active_revision_.load(std::memory_order_acquire);
    if (revision == previous) return;
    active_revision_.store(revision, std::memory_order_release);
    epoch_.fetch_add(1, std::memory_order_acq_rel);
    latest_sequence_.fetch_add(1, std::memory_order_acq_rel);
    revision_invalidations_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        clear_cache_locked();
    }
    cv_.notify_all();
}

void ProductionTimelinePlayback::invalidate_all() {
    epoch_.fetch_add(1, std::memory_order_acq_rel);
    latest_sequence_.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        clear_cache_locked();
    }
    cv_.notify_all();
}

std::uint64_t ProductionTimelinePlayback::epoch() const noexcept {
    return epoch_.load(std::memory_order_acquire);
}

std::uint64_t ProductionTimelinePlayback::active_revision() const noexcept {
    return active_revision_.load(std::memory_order_acquire);
}

TimelinePlaybackTelemetry ProductionTimelinePlayback::telemetry() const noexcept {
    return {requested_.load(), completed_.load(), stale_rejected_.load(),
            cache_hits_.load(), cache_misses_.load(), revision_invalidations_.load(),
            seek_invalidations_.load()};
}

TimelineExecutionPlan ProductionTimelinePlayback::build_preview_plan(
    std::int64_t timestamp_us, std::int64_t audio_window_duration_us) const noexcept {
    const auto snapshot = publisher_->acquire();
    if (snapshot) {
        return build_plan(*snapshot, timestamp_us, audio_window_duration_us);
    }

    TimelineExecutionPlan plan;
    plan.status = TimelineEvaluationStatus::invalid_timeline;
    plan.diagnostic = "no published timeline snapshot";
    return plan;
}

TimelineExecutionPlan ProductionTimelinePlayback::build_export_plan(
    std::int64_t timestamp_us, std::int64_t audio_window_duration_us) const noexcept {
    return build_preview_plan(timestamp_us, audio_window_duration_us);
}

bool ProductionTimelinePlayback::current(
    const TimelinePlaybackRequest& request_value) const noexcept {
    return request_value.epoch == epoch_.load(std::memory_order_acquire) &&
           request_value.sequence == latest_sequence_.load(std::memory_order_acquire) &&
           request_value.revision == active_revision_.load(std::memory_order_acquire);
}

TimelineExecutionPlan ProductionTimelinePlayback::build_plan(
    const ProductionTimelineSnapshot& snapshot,
    std::int64_t timestamp_us,
    std::int64_t audio_window_duration_us) const noexcept {
    return build_timeline_execution_plan(snapshot, timestamp_us, audio_window_duration_us);
}

void ProductionTimelinePlayback::clear_cache_locked() {
    plan_cache_.clear();
    cache_order_.clear();
}

void ProductionTimelinePlayback::run() noexcept {
    for (;;) {
        TimelinePlaybackRequest request_value;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || (!paused_ && !queue_.empty()); });
            if (stopping_) return;
            request_value = queue_.front();
            queue_.pop_front();
        }
        if (!current(request_value)) {
            stale_rejected_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        try {
            const auto snapshot = publisher_->acquire();
            if (!snapshot || snapshot->revision != request_value.revision) {
                throw std::runtime_error("timeline snapshot revision changed during playback");
            }
            const PlanCacheKey key{request_value.revision, request_value.timestamp_us,
                                   request_value.audio_window_duration_us};
            TimelineExecutionPlan plan;
            bool cache_hit = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto found = plan_cache_.find(key);
                if (found != plan_cache_.end()) {
                    plan = found->second;
                    cache_hit = true;
                }
            }
            if (cache_hit) {
                cache_hits_.fetch_add(1, std::memory_order_relaxed);
            } else {
                cache_misses_.fetch_add(1, std::memory_order_relaxed);
                plan = build_plan(*snapshot, request_value.timestamp_us,
                                  request_value.audio_window_duration_us);
                if (cache_capacity_ > 0 && plan.status == TimelineEvaluationStatus::ok) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (plan_cache_.size() >= cache_capacity_ && !cache_order_.empty()) {
                        plan_cache_.erase(cache_order_.front());
                        cache_order_.pop_front();
                    }
                    plan_cache_[key] = plan;
                    cache_order_.push_back(key);
                }
            }
            if (!current(request_value)) {
                stale_rejected_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            CompletionCallback completion;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                completion = completion_;
            }
            completed_.fetch_add(1, std::memory_order_relaxed);
            if (completion) completion({request_value, std::move(plan), cache_hit});
        } catch (...) {
            if (!current(request_value)) {
                stale_rejected_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            ErrorCallback error;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                error = error_;
            }
            if (error) error(request_value, std::current_exception());
        }
    }
}

} // namespace digitor
