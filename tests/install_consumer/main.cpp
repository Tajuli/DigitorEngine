#include <digitor/sdk_worker_c_api.h>

int main() {
  DigitorSdkWorkerHandle* worker = digitor_sdk_worker_create(
      DIGITOR_SDK_WORKER_PREVIEW, 1, nullptr, nullptr, nullptr);
  if (!worker) return 1;
  if (!digitor_sdk_worker_start(worker)) return 2;
  digitor_sdk_worker_destroy(worker);
  return 0;
}
