#include <digitor/sdk_worker_c_api.h>

#include <atomic>

namespace {
std::atomic<int> completion_count{0};

void on_completion(void*, DigitorSdkWorkerCompletion completion) {
  if (completion == DIGITOR_SDK_WORKER_COMPLETED) {
    completion_count.fetch_add(1, std::memory_order_relaxed);
  }
}
} // namespace

int main() {
  DigitorSdkWorkerHandle* worker = digitor_sdk_worker_create(
      DIGITOR_SDK_WORKER_PREVIEW, 1, nullptr, on_completion, nullptr);
  if (!worker) return 1;
  if (!digitor_sdk_worker_start(worker)) {
    digitor_sdk_worker_destroy(worker);
    return 2;
  }
  digitor_sdk_worker_destroy(worker);
  return completion_count.load(std::memory_order_relaxed) == 1 ? 0 : 3;
}
