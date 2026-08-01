#include "digitor/sdk_media_worker.hpp"

#include <utility>

namespace digitor {

SdkMediaWorker::SdkMediaWorker(SdkMediaJobKind kind,
                               std::uint64_t total_units,
                               ProgressCallback progress,
                               CompletionCallback completion)
    : kind_(kind),
      total_units_(total_units),
      progress_(std::move(progress)),
      operation_(std::make_shared<AsyncOperation>(std::move(completion))) {}

SdkMediaWorker::~SdkMediaWorker() {
  dispose();
  join();
}

bool SdkMediaWorker::start() {
  bool expected = false;
  if (!started_.compare_exchange_strong(expected, true)) return false;
  if (disposed_.load(std::memory_order_acquire)) return false;

  try {
    worker_ = std::thread([this] { run(); });
  } catch (...) {
    (void)operation_->complete(AsyncCompletion::failed);
    return false;
  }
  return true;
}

bool SdkMediaWorker::cancel() noexcept {
  if (disposed_.load(std::memory_order_acquire)) return false;
  return operation_->cancel();
}

void SdkMediaWorker::dispose() noexcept {
  bool expected = false;
  if (!disposed_.compare_exchange_strong(expected, true)) return;
  {
    std::scoped_lock lock(mutex_);
    progress_ = {};
  }
  operation_->dispose();
}

void SdkMediaWorker::join() noexcept {
  if (worker_.joinable()) worker_.join();
}

bool SdkMediaWorker::running() const noexcept {
  return started_.load(std::memory_order_acquire) &&
         !operation_->is_completed();
}

std::uint64_t SdkMediaWorker::delivered_callbacks() const noexcept {
  return operation_->delivered_callbacks();
}

void SdkMediaWorker::run() noexcept {
  for (std::uint64_t completed = 1; completed <= total_units_; ++completed) {
    if (operation_->is_completed() ||
        disposed_.load(std::memory_order_acquire)) {
      return;
    }

    ProgressCallback progress;
    {
      std::scoped_lock lock(mutex_);
      if (!disposed_.load(std::memory_order_relaxed)) progress = progress_;
    }
    if (progress) {
      try {
        progress({kind_, completed, total_units_});
      } catch (...) {
      }
    }
  }

  if (!disposed_.load(std::memory_order_acquire)) {
    (void)operation_->complete(AsyncCompletion::completed);
  }
}

}  // namespace digitor
