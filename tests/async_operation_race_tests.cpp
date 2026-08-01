#include "digitor/async_operation.hpp"

#include <cassert>
#include <atomic>
#include <thread>
#include <vector>

int main() {
  using namespace digitor;

  for (int iteration = 0; iteration < 2000; ++iteration) {
    std::atomic<int> callback_count{0};
    std::atomic<int> cancelled_count{0};
    AsyncOperation operation([&](AsyncCompletion result) {
      callback_count.fetch_add(1, std::memory_order_relaxed);
      if (result == AsyncCompletion::cancelled) {
        cancelled_count.fetch_add(1, std::memory_order_relaxed);
      }
    });

    std::thread canceller([&] { (void)operation.cancel(); });
    std::thread completer([&] { (void)operation.complete(AsyncCompletion::completed); });
    std::thread disposer([&] { operation.dispose(); });
    canceller.join();
    completer.join();
    disposer.join();

    assert(operation.is_completed());
    assert(callback_count.load() <= 1);
    assert(operation.delivered_callbacks() <= 1);
    assert(cancelled_count.load() <= 1);
    assert(!operation.cancel());
    assert(!operation.complete(AsyncCompletion::failed));
  }

  std::atomic<int> callback_count{0};
  AsyncOperation disposed([&](AsyncCompletion) { callback_count.fetch_add(1); });
  disposed.dispose();
  assert(disposed.complete(AsyncCompletion::completed));
  assert(callback_count.load() == 0);
  assert(disposed.delivered_callbacks() == 0);

  return 0;
}
