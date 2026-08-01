#include "digitor/sdk_worker_c_api.h"
#include "digitor/sdk_media_worker.hpp"

#include <memory>
#include <mutex>
#include <new>
#include <unordered_set>

struct DigitorSdkWorkerHandle {
  std::unique_ptr<digitor::SdkMediaWorker> worker;
};

namespace {
std::mutex handles_mutex;
std::unordered_set<DigitorSdkWorkerHandle*> handles;

bool valid(DigitorSdkWorkerHandle* handle) {
  std::scoped_lock lock(handles_mutex);
  return handle && handles.contains(handle);
}
}

extern "C" DigitorSdkWorkerHandle* digitor_sdk_worker_create(
    DigitorSdkWorkerKind kind, uint64_t total_units,
    DigitorSdkWorkerProgressCallback progress_callback,
    DigitorSdkWorkerCompletionCallback completion_callback, void* user_data) {
  if (total_units == 0 || !completion_callback) return nullptr;
  try {
    auto handle = std::make_unique<DigitorSdkWorkerHandle>();
    const auto native_kind = kind == DIGITOR_SDK_WORKER_EXPORT
                                 ? digitor::SdkMediaJobKind::export_job
                                 : digitor::SdkMediaJobKind::preview;
    handle->worker = std::make_unique<digitor::SdkMediaWorker>(
        native_kind, total_units,
        [progress_callback, user_data](const digitor::SdkMediaProgress& progress) {
          if (progress_callback) {
            progress_callback(user_data, progress.completed_units,
                              progress.total_units);
          }
        },
        [completion_callback, user_data](digitor::AsyncCompletion completion) {
          DigitorSdkWorkerCompletion result = DIGITOR_SDK_WORKER_FAILED;
          if (completion == digitor::AsyncCompletion::completed) {
            result = DIGITOR_SDK_WORKER_COMPLETED;
          } else if (completion == digitor::AsyncCompletion::cancelled) {
            result = DIGITOR_SDK_WORKER_CANCELLED;
          }
          completion_callback(user_data, result);
        });
    auto* raw = handle.release();
    {
      std::scoped_lock lock(handles_mutex);
      handles.insert(raw);
    }
    return raw;
  } catch (...) {
    return nullptr;
  }
}

extern "C" int32_t digitor_sdk_worker_start(DigitorSdkWorkerHandle* handle) {
  if (!valid(handle)) return 0;
  try {
    return handle->worker->start() ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

extern "C" int32_t digitor_sdk_worker_cancel(DigitorSdkWorkerHandle* handle) {
  if (!valid(handle)) return 0;
  return handle->worker->cancel() ? 1 : 0;
}

extern "C" void digitor_sdk_worker_destroy(DigitorSdkWorkerHandle* handle) {
  if (!handle) return;
  {
    std::scoped_lock lock(handles_mutex);
    if (!handles.erase(handle)) return;
  }
  handle->worker->dispose();
  handle->worker->join();
  delete handle;
}
