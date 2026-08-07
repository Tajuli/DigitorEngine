#include <digitor/digitor.h>
#include <digitor/sdk_worker_c_api.h>

#include <cstring>

namespace {
void on_completion(void*, DigitorSdkWorkerCompletion) {}
} // namespace

int main() {
  if (std::strcmp(digitor_get_version(), "0.0.1") != 0) return 10;

  DigitorSdkWorkerHandle* worker = digitor_sdk_worker_create(
      DIGITOR_SDK_WORKER_PREVIEW, 1, nullptr, on_completion, nullptr);
  if (!worker) return 1;
  if (!digitor_sdk_worker_start(worker)) {
    digitor_sdk_worker_destroy(worker);
    return 2;
  }
  digitor_sdk_worker_destroy(worker);
  return 0;
}
