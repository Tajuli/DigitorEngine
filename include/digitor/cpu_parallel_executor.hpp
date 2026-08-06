#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace digitor {

struct CpuTile2D {
  std::size_t x_begin{};
  std::size_t y_begin{};
  std::size_t x_end{};
  std::size_t y_end{};
  std::size_t read_x_begin{};
  std::size_t read_y_begin{};
  std::size_t read_x_end{};
  std::size_t read_y_end{};
};

// Persistent deterministic scheduler shared by CPU render, image, video,
// effects, masks and analysis paths. Every output element or tile has one
// owner. Node order remains sequential; work inside a node is parallel.
class CpuParallelExecutor final {
 public:
  explicit CpuParallelExecutor(std::size_t requested_workers = 0) {
    const auto hardware =
        static_cast<std::size_t>(std::thread::hardware_concurrency());
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
    const std::size_t task_count =
        std::min(workers_.size(), (count + grain - 1) / grain);
    if (task_count <= 1 || inside_worker_) {
      function(0, count);
      return;
    }

    // One executor may be reached by concurrent preview/export callers. Jobs
    // are serialized at submission while their pixel work remains parallel.
    std::unique_lock submission_lock(submission_mutex_);
    std::function<void(std::size_t, std::size_t)> job(
        std::forward<Function>(function));
    {
      std::lock_guard lock(mutex_);
      count_ = count;
      task_count_ = task_count;
      job_ = std::move(job);
      exception_ = nullptr;
      next_task_.store(0, std::memory_order_relaxed);
      remaining_.store(task_count, std::memory_order_release);
      ++generation_;
    }
    work_cv_.notify_all();
    execute_tasks();

    std::unique_lock lock(mutex_);
    done_cv_.wait(lock, [this] {
      return remaining_.load(std::memory_order_acquire) == 0;
    });
    job_ = {};
    const auto error = exception_;
    lock.unlock();
    if (error) std::rethrow_exception(error);
  }

  template <typename Function>
  void parallel_for_rows(std::size_t height, std::size_t minimum_rows,
                         Function&& function) {
    parallel_for(height, minimum_rows, std::forward<Function>(function));
  }

  template <typename Function>
  void parallel_for_tiles(std::size_t width, std::size_t height,
                          std::size_t tile_width, std::size_t tile_height,
                          std::size_t halo, Function&& function) {
    if (width == 0 || height == 0) return;
    tile_width = std::max<std::size_t>(1, tile_width);
    tile_height = std::max<std::size_t>(1, tile_height);
    const std::size_t columns = (width + tile_width - 1) / tile_width;
    const std::size_t rows = (height + tile_height - 1) / tile_height;
    const std::size_t tile_count = columns * rows;
    parallel_for(tile_count, 1, [&](std::size_t begin, std::size_t end) {
      for (std::size_t index = begin; index < end; ++index) {
        const std::size_t tx = index % columns;
        const std::size_t ty = index / columns;
        CpuTile2D tile;
        tile.x_begin = tx * tile_width;
        tile.y_begin = ty * tile_height;
        tile.x_end = std::min(width, tile.x_begin + tile_width);
        tile.y_end = std::min(height, tile.y_begin + tile_height);
        tile.read_x_begin = tile.x_begin > halo ? tile.x_begin - halo : 0;
        tile.read_y_begin = tile.y_begin > halo ? tile.y_begin - halo : 0;
        tile.read_x_end = std::min(width, tile.x_end + halo);
        tile.read_y_end = std::min(height, tile.y_end + halo);
        function(tile);
      }
    });
  }

  // Fixed chunk indices and an ordered final merge make reductions independent
  // of worker completion order.
  template <typename Value, typename Map, typename Merge>
  Value deterministic_reduce(std::size_t count, std::size_t minimum_grain,
                             Value identity, Map&& map, Merge&& merge) {
    if (count == 0) return identity;
    const std::size_t grain = std::max<std::size_t>(1, minimum_grain);
    const std::size_t chunks =
        std::max<std::size_t>(1, std::min(workers_.size(),
                                         (count + grain - 1) / grain));
    std::vector<Value> partials(chunks, identity);
    parallel_for(chunks, 1, [&](std::size_t begin_chunk,
                                std::size_t end_chunk) {
      for (std::size_t chunk = begin_chunk; chunk < end_chunk; ++chunk) {
        const std::size_t begin = count * chunk / chunks;
        const std::size_t end = count * (chunk + 1) / chunks;
        partials[chunk] = map(begin, end);
      }
    });
    Value result = identity;
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
      result = merge(std::move(result), std::move(partials[chunk]));
    }
    return result;
  }

 private:
  void worker_loop() {
    std::uint64_t observed_generation = 0;
    inside_worker_ = true;
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

  void execute_tasks() noexcept {
    const bool was_inside = inside_worker_;
    inside_worker_ = true;
    for (;;) {
      const std::size_t task =
          next_task_.fetch_add(1, std::memory_order_relaxed);
      if (task >= task_count_) break;
      const std::size_t begin = count_ * task / task_count_;
      const std::size_t end = count_ * (task + 1) / task_count_;
      try {
        job_(begin, end);
      } catch (...) {
        std::lock_guard lock(mutex_);
        if (!exception_) exception_ = std::current_exception();
      }
      if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        done_cv_.notify_one();
      }
    }
    inside_worker_ = was_inside;
  }

  inline static thread_local bool inside_worker_ = false;
  std::vector<std::thread> workers_;
  std::mutex submission_mutex_;
  mutable std::mutex mutex_;
  std::condition_variable work_cv_;
  std::condition_variable done_cv_;
  std::function<void(std::size_t, std::size_t)> job_;
  std::exception_ptr exception_;
  std::atomic<std::size_t> next_task_{0};
  std::atomic<std::size_t> remaining_{0};
  std::size_t count_{};
  std::size_t task_count_{};
  std::uint64_t generation_{};
  bool stopping_{};
};

inline CpuParallelExecutor& shared_cpu_executor() {
  static CpuParallelExecutor executor;
  return executor;
}

}  // namespace digitor
