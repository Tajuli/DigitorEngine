#include "digitor/sdk_media_worker.hpp"

#include <utility>

namespace digitor {

SdkMediaWorker::SdkMediaWorker(SdkMediaJobKind kind, std::uint64_t total_units,
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
  if (total_units_ == 0 || disposed_.load(std::memory_order_acquire)) return false;
  bool expected = false;
  if (!started_.compare_exchange_strong(expected, true)) return false;
  try {
    worker_ = std::thread([this] { run(); });
  } catch (...) {
    started_.store(false, std::memory_order_release);
    operation_->complete(AsyncCompletion::failed);
    return false;
  }
  return true;
}

bool SdkMediaWorker::cancel() noexcept { return operation_->cancel(); }

void SdkMediaWorker::dispose() noexcept {
  disposed_.store(true, std::memory_order_release);
  operation_->dispose();
  std::scoped_lock lock(mutex_);
  progress_ = {};
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
  for (std::uint64_t unit = 1; unit <= total_units_; ++unit) {
    if (operation_->is_completed() || disposed_.load(std::memory_order_acquire)) return;

    ProgressCallback progress;
    {
      std::scoped_lock lock(mutex_);
      if (!disposed_.load(std::memory_order_relaxed)) progress = progress_;
    }
    if (progress) {
      try {
        progress({kind_, unit, total_units_});
      } catch (...) {
        operation_->complete(AsyncCompletion::failed);
        return;
      }
    }
  }
  operation_->complete(AsyncCompletion::completed);
}

}  // namespace digitor
