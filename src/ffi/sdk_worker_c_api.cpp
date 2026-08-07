#include "digitor/sdk_worker_c_api.h"
#include "digitor/sdk_media_worker.hpp"

#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>

struct DigitorSdkWorkerHandle {
  std::mutex api_mutex;
  std::unique_ptr<digitor::SdkMediaWorker> worker;
  bool active{true};
};

namespace {
std::mutex handles_mutex;
std::unordered_map<DigitorSdkWorkerHandle*,
                   std::shared_ptr<DigitorSdkWorkerHandle>> handles;

std::shared_ptr<DigitorSdkWorkerHandle> retain(
    DigitorSdkWorkerHandle* handle) {
  if (!handle) return {};
  std::scoped_lock lock(handles_mutex);
  const auto it = handles.find(handle);
  return it == handles.end() ? std::shared_ptr<DigitorSdkWorkerHandle>{}
                             : it->second;
}
}  // namespace

extern "C" DigitorSdkWorkerHandle* digitor_sdk_worker_create(
    DigitorSdkWorkerKind kind, uint64_t total_units,
    DigitorSdkWorkerProgressCallback progress_callback,
    DigitorSdkWorkerCompletionCallback completion_callback, void* user_data) {
  if (total_units == 0 || !completion_callback) return nullptr;
  try {
    auto handle = std::make_shared<DigitorSdkWorkerHandle>();
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
    auto* raw = handle.get();
    {
      std::scoped_lock lock(handles_mutex);
      if (!handles.emplace(raw, handle).second) return nullptr;
    }
    return raw;
  } catch (...) {
    return nullptr;
  }
}

extern "C" int32_t digitor_sdk_worker_start(DigitorSdkWorkerHandle* handle) {
  auto retained = retain(handle);
  if (!retained) return 0;
  try {
    // Start must serialize with destroy because std::thread construction and
    // join touch the worker's thread object. Retained shared ownership keeps
    // the opaque handle alive after the registry lock is released.
    std::scoped_lock lock(retained->api_mutex);
    if (!retained->active || !retained->worker) return 0;
    return retained->worker->start() ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

extern "C" int32_t digitor_sdk_worker_cancel(DigitorSdkWorkerHandle* handle) {
  auto retained = retain(handle);
  if (!retained || !retained->worker) return 0;
  try {
    // SdkMediaWorker::cancel() may synchronously deliver the completion
    // callback. Do not hold api_mutex across user callbacks: a callback is
    // allowed to re-enter the C API (including destroy) without deadlocking.
    // The retained shared_ptr guarantees handle/worker lifetime for this call.
    return retained->worker->cancel() ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

extern "C" void digitor_sdk_worker_destroy(DigitorSdkWorkerHandle* handle) {
  if (!handle) return;

  std::shared_ptr<DigitorSdkWorkerHandle> retained;
  {
    std::scoped_lock lock(handles_mutex);
    const auto it = handles.find(handle);
    if (it == handles.end()) return;
    retained = std::move(it->second);
    handles.erase(it);
  }

  try {
    // Registry removal makes all new API calls fail retention. The API lock
    // waits for any in-flight start() to finish before dispose/join begins.
    std::scoped_lock lock(retained->api_mutex);
    if (!retained->active || !retained->worker) return;
    retained->active = false;
    retained->worker->dispose();
    retained->worker->join();
  } catch (...) {
    // C ABI destruction must never propagate exceptions across the boundary.
  }
}
