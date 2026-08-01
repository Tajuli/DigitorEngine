#include "digitor/async_operation.hpp"

#include <utility>

namespace digitor {

AsyncOperation::AsyncOperation(Callback callback) : callback_(std::move(callback)) {}

bool AsyncOperation::cancel() noexcept {
  bool expected = false;
  if (!cancelled_.compare_exchange_strong(expected, true)) return false;
  return complete(AsyncCompletion::cancelled);
}

bool AsyncOperation::complete(AsyncCompletion result) noexcept {
  bool expected = false;
  if (!completed_.compare_exchange_strong(expected, true)) return false;

  Callback callback;
  {
    std::scoped_lock lock(mutex_);
    if (!disposed_) callback = callback_;
  }
  if (callback) {
    try {
      callback(result);
      delivered_callbacks_.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
    }
  }
  return true;
}

void AsyncOperation::dispose() noexcept {
  disposed_.store(true, std::memory_order_release);
  std::scoped_lock lock(mutex_);
  callback_ = {};
}

bool AsyncOperation::is_cancelled() const noexcept {
  return cancelled_.load(std::memory_order_acquire);
}

bool AsyncOperation::is_completed() const noexcept {
  return completed_.load(std::memory_order_acquire);
}

std::uint64_t AsyncOperation::delivered_callbacks() const noexcept {
  return delivered_callbacks_.load(std::memory_order_acquire);
}

}  // namespace digitor
