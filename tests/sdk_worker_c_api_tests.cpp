#include "digitor/sdk_worker_c_api.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

struct State {
  std::atomic<uint64_t> progress{0};
  std::atomic<int> completions{0};
  std::atomic<int> result{-1};
};

void on_progress(void* user, uint64_t completed, uint64_t total) {
  auto* state = static_cast<State*>(user);
  assert(completed <= total);
  state->progress.store(completed);
}

void on_complete(void* user, DigitorSdkWorkerCompletion completion) {
  auto* state = static_cast<State*>(user);
  state->result.store(static_cast<int>(completion));
  state->completions.fetch_add(1);
}

int main() {
  assert(digitor_sdk_worker_create(DIGITOR_SDK_WORKER_PREVIEW, 0,
                                   on_progress, on_complete, nullptr) == nullptr);

  State completed;
  auto* preview = digitor_sdk_worker_create(DIGITOR_SDK_WORKER_PREVIEW, 64,
                                             on_progress, on_complete, &completed);
  assert(preview != nullptr);
  assert(digitor_sdk_worker_start(preview) == 1);
  assert(digitor_sdk_worker_start(preview) == 0);
  for (int i = 0; i < 200 && completed.completions.load() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(completed.completions.load() == 1);
  assert(completed.result.load() == DIGITOR_SDK_WORKER_COMPLETED);
  digitor_sdk_worker_destroy(preview);
  digitor_sdk_worker_destroy(preview);

  State cancelled;
  auto* export_job = digitor_sdk_worker_create(DIGITOR_SDK_WORKER_EXPORT, 100000,
                                                on_progress, on_complete, &cancelled);
  assert(export_job != nullptr);
  assert(digitor_sdk_worker_start(export_job) == 1);
  assert(digitor_sdk_worker_cancel(export_job) == 1);
  assert(digitor_sdk_worker_cancel(export_job) == 0);
  for (int i = 0; i < 200 && cancelled.completions.load() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(cancelled.completions.load() == 1);
  assert(cancelled.result.load() == DIGITOR_SDK_WORKER_CANCELLED);
  digitor_sdk_worker_destroy(export_job);

  assert(digitor_sdk_worker_start(nullptr) == 0);
  assert(digitor_sdk_worker_cancel(nullptr) == 0);
  return 0;
}
