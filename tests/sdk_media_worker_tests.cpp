#include "digitor/sdk_media_worker.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

int main() {
  using namespace digitor;

  {
    std::atomic<std::uint64_t> progress{0};
    std::atomic<std::uint64_t> completed{0};
    SdkMediaWorker preview(
        SdkMediaJobKind::preview, 1000,
        [&](const SdkMediaProgress& value) {
          assert(value.kind == SdkMediaJobKind::preview);
          assert(value.completed_units <= value.total_units);
          progress.fetch_add(1, std::memory_order_relaxed);
        },
        [&](AsyncCompletion result) {
          assert(result == AsyncCompletion::completed);
          completed.fetch_add(1, std::memory_order_relaxed);
        });
    assert(preview.start());
    assert(!preview.start());
    preview.join();
    assert(progress.load() == 1000);
    assert(completed.load() == 1);
    assert(preview.delivered_callbacks() == 1);
  }

  {
    std::atomic<std::uint64_t> cancelled{0};
    auto worker = std::make_unique<SdkMediaWorker>(
        SdkMediaJobKind::export_job, 5'000'000,
        [](const SdkMediaProgress&) {},
        [&](AsyncCompletion result) {
          assert(result == AsyncCompletion::cancelled);
          cancelled.fetch_add(1, std::memory_order_relaxed);
        });
    assert(worker->start());
    assert(worker->cancel());
    assert(!worker->cancel());
    worker->join();
    assert(cancelled.load() == 1);
    assert(worker->delivered_callbacks() == 1);
  }

  {
    std::atomic<std::uint64_t> callbacks{0};
    std::atomic<std::uint64_t> progress{0};
    SdkMediaWorker worker(
        SdkMediaJobKind::export_job, 10'000'000,
        [&](const SdkMediaProgress&) {
          progress.fetch_add(1, std::memory_order_relaxed);
        },
        [&](AsyncCompletion) {
          callbacks.fetch_add(1, std::memory_order_relaxed);
        });
    assert(worker.start());
    worker.dispose();
    const auto observed = progress.load(std::memory_order_acquire);
    worker.join();
    assert(progress.load(std::memory_order_acquire) == observed);
    assert(callbacks.load() == 0);
  }

  for (int iteration = 0; iteration < 1000; ++iteration) {
    std::atomic<std::uint64_t> callbacks{0};
    SdkMediaWorker worker(
        SdkMediaJobKind::export_job, 100'000,
        [](const SdkMediaProgress&) {},
        [&](AsyncCompletion) {
          callbacks.fetch_add(1, std::memory_order_relaxed);
        });
    assert(worker.start());
    std::thread cancel_thread([&] { (void)worker.cancel(); });
    std::thread dispose_thread([&] { worker.dispose(); });
    cancel_thread.join();
    dispose_thread.join();
    worker.join();
    assert(callbacks.load() <= 1);
    assert(worker.delivered_callbacks() <= 1);
  }

  return 0;
}
