#include "digitor/cpu_parallel_executor.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

std::vector<std::uint64_t> render_fixture(std::size_t workers) {
  digitor::CpuParallelExecutor executor(workers);
  constexpr std::size_t count = 1U << 20U;
  std::vector<std::uint64_t> output(count);
  executor.parallel_for(count, 4096, [&](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
      std::uint64_t value = static_cast<std::uint64_t>(i) + 0x9e3779b97f4a7c15ULL;
      value ^= value >> 30U;
      value *= 0xbf58476d1ce4e5b9ULL;
      value ^= value >> 27U;
      value *= 0x94d049bb133111ebULL;
      output[i] = value ^ (value >> 31U);
    }
  });
  const auto telemetry = executor.telemetry();
  assert(telemetry.submitted_jobs == 1U);
  assert(telemetry.processed_items == count);
  assert(telemetry.worker_count == std::clamp<std::size_t>(workers, 1U, 32U));
  return output;
}

void verify_worker_matrix_determinism() {
  const auto reference = render_fixture(1);
  for (const std::size_t workers : {2U, 4U, 8U, 16U}) {
    assert(render_fixture(workers) == reference);
  }
}

void verify_tiles_and_halos() {
  digitor::CpuParallelExecutor executor(4);
  constexpr std::size_t width = 257;
  constexpr std::size_t height = 131;
  std::vector<std::atomic<unsigned>> owners(width * height);
  for (auto& owner : owners) owner.store(0, std::memory_order_relaxed);
  executor.parallel_for_tiles(width, height, 32, 17, 3,
                              [&](const digitor::CpuTile2D& tile) {
    assert(tile.read_x_begin <= tile.x_begin);
    assert(tile.read_y_begin <= tile.y_begin);
    assert(tile.read_x_end >= tile.x_end);
    assert(tile.read_y_end >= tile.y_end);
    for (std::size_t y = tile.y_begin; y < tile.y_end; ++y) {
      for (std::size_t x = tile.x_begin; x < tile.x_end; ++x) {
        owners[y * width + x].fetch_add(1, std::memory_order_relaxed);
      }
    }
  });
  for (const auto& owner : owners) {
    assert(owner.load(std::memory_order_relaxed) == 1U);
  }
}

void verify_ordered_reduction() {
  std::uint64_t expected = 0;
  constexpr std::size_t count = 500000;
  for (std::size_t i = 0; i < count; ++i) expected += (i * 17U) ^ (i >> 3U);
  for (const std::size_t workers : {1U, 2U, 4U, 8U}) {
    digitor::CpuParallelExecutor executor(workers);
    const auto actual = executor.deterministic_reduce<std::uint64_t>(
        count, 4096, 0,
        [](std::size_t begin, std::size_t end) {
          std::uint64_t partial = 0;
          for (std::size_t i = begin; i < end; ++i) {
            partial += (i * 17U) ^ (i >> 3U);
          }
          return partial;
        },
        [](std::uint64_t a, std::uint64_t b) { return a + b; });
    assert(actual == expected);
  }
}

void verify_nested_calls_and_exception_propagation() {
  digitor::CpuParallelExecutor executor(4);
  std::atomic<std::size_t> visited{0};
  executor.parallel_for(64, 1, [&](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
      executor.parallel_for(8, 1, [&](std::size_t nested_begin,
                                      std::size_t nested_end) {
        visited.fetch_add(nested_end - nested_begin, std::memory_order_relaxed);
      });
    }
  });
  assert(visited.load(std::memory_order_relaxed) == 512U);
  assert(executor.telemetry().nested_serial_jobs > 0U);

  bool threw = false;
  try {
    executor.parallel_for(128, 1, [](std::size_t begin, std::size_t end) {
      if (begin <= 64 && end > 64) throw std::runtime_error("qualification");
    });
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);
}

void verify_concurrent_submission_and_lifecycle() {
  for (unsigned iteration = 0; iteration < 20; ++iteration) {
    digitor::CpuParallelExecutor executor(4);
    std::atomic<std::uint64_t> total{0};
    auto submit = [&] {
      executor.parallel_for(200000, 2048,
                            [&](std::size_t begin, std::size_t end) {
        total.fetch_add(end - begin, std::memory_order_relaxed);
      });
    };
    std::thread preview(submit);
    std::thread export_job(submit);
    preview.join();
    export_job.join();
    assert(total.load(std::memory_order_relaxed) == 400000U);
    assert(executor.telemetry().submitted_jobs == 2U);
  }
}

void report_non_gating_benchmark() {
  constexpr std::size_t count = 1U << 22U;
  for (const std::size_t workers : {1U, 2U, 4U, 8U}) {
    digitor::CpuParallelExecutor executor(workers);
    std::vector<std::uint64_t> data(count);
    const auto start = std::chrono::steady_clock::now();
    executor.parallel_for(count, 8192, [&](std::size_t begin, std::size_t end) {
      for (std::size_t i = begin; i < end; ++i) {
        data[i] = (i * 0x9e3779b97f4a7c15ULL) ^ (i >> 7U);
      }
    });
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    const auto telemetry = executor.telemetry();
    assert(telemetry.elapsed_nanoseconds > 0U);
    std::cout << "workers=" << workers << " elapsed_us=" << elapsed.count()
              << " tasks=" << telemetry.executed_tasks
              << " items=" << telemetry.processed_items << '\n';
  }
}

}  // namespace

int main() {
  verify_worker_matrix_determinism();
  verify_tiles_and_halos();
  verify_ordered_reduction();
  verify_nested_calls_and_exception_propagation();
  verify_concurrent_submission_and_lifecycle();
  report_non_gating_benchmark();
  std::cout << "CPU parallel qualification passed\n";
  return 0;
}
