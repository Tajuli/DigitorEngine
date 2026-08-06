#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace digitor {

// Persistent deterministic CPU scheduler used by CPU rendering paths. Work is
// partitioned into fixed contiguous ranges; each output element is owned by one
// task, so scheduling order cannot change pixel results.
class CpuParallelExecutor final {
 public:
  explicit CpuParallelExecutor(std::size_t requested_workers = 0) {
    const auto hardware = static_cast<std::size_t>(std::thread::hardware_concurrency());
    std::size_t workers = requested_workers;
    if (workers == 0) workers = hardware > 1 ? hardware - 1 : 1;
    workers = std::clamp<std::size_t>(workers, 1, 32);
    workers_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) {
      workers_.emplace_back([this] { worker_loop(); });
    }
  }

  CpuParallelExecutor(const CpuParallelExecutor&) = delete;
  CpuParallelExecutor& operator=(const CpuParallelExecutor&) = delete;

  ~CpuParallelExecutor() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
      ++generation_;
    }
    work_cv_.notify_all();
    for (auto& worker : workers_) {
      if (worker.joinable()) worker.join();
    }
  }

  [[nodiscard]] std::size_t worker_count() const noexcept {
    return workers_.size();
  }

  template <typename Function>
  void parallel_for(std::size_t count, std::size_t minimum_grain,
                    Function&& function) {
    if (count == 0) return;
    const std::size_t grain = std::max<std::size_t>(1, minimum_grain);
    const std::size_t task_count = std::min(
        workers_.size(), (count + grain - 1) / grain);
    if (task_count <= 1) {
      function(0, count);
      return;
    }

    std::function<void(std::size_t, std::size_t)> job(
        std::forward<Function>(function));
    {
      std::lock_guard lock(mutex_);
      count_ = count;
      task_count_ = task_count;
      job_ = std::move(job);
      next_task_.store(0, std::memory_order_relaxed);
      remaining_.store(task_count, std::memory_order_release);
      ++generation_;
    }
    work_cv_.notify_all();

    // The caller also participates, reducing latency on small preview frames.
    execute_tasks();

    std::unique_lock lock(mutex_);
    done_cv_.wait(lock, [this] {
      return remaining_.load(std::memory_order_acquire) == 0;
    });
    job_ = {};
  }

 private:
  void worker_loop() {
    std::uint64_t observed_generation = 0;
    for (;;) {
      {
        std::unique_lock lock(mutex_);
        work_cv_.wait(lock, [this, &observed_generation] {
          return stopping_ || generation_ != observed_generation;
        });
        if (stopping_) return;
        observed_generation = generation_;
      }
      execute_tasks();
    }
  }

  void execute_tasks() {
    for (;;) {
      const std::size_t task = next_task_.fetch_add(1, std::memory_order_relaxed);
      if (task >= task_count_) break;
      const std::size_t begin = count_ * task / task_count_;
      const std::size_t end = count_ * (task + 1) / task_count_;
      job_(begin, end);
      if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        done_cv_.notify_one();
      }
    }
  }

  std::vector<std::thread> workers_;
  mutable std::mutex mutex_;
  std::condition_variable work_cv_;
  std::condition_variable done_cv_;
  std::function<void(std::size_t, std::size_t)> job_;
  std::atomic<std::size_t> next_task_{0};
  std::atomic<std::size_t> remaining_{0};
  std::size_t count_{};
  std::size_t task_count_{};
  std::uint64_t generation_{};
  bool stopping_{};
};

}  // namespace digitor
