#ifndef DIGITOR_ASYNC_OPERATION_HPP
#define DIGITOR_ASYNC_OPERATION_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

namespace digitor {

enum class AsyncCompletion { completed, cancelled, failed };

class AsyncOperation final {
 public:
  using Callback = std::function<void(AsyncCompletion)>;

  explicit AsyncOperation(Callback callback);
  AsyncOperation(const AsyncOperation&) = delete;
  AsyncOperation& operator=(const AsyncOperation&) = delete;

  bool cancel() noexcept;
  bool complete(AsyncCompletion result) noexcept;
  void dispose() noexcept;

  [[nodiscard]] bool is_cancelled() const noexcept;
  [[nodiscard]] bool is_completed() const noexcept;
  [[nodiscard]] std::uint64_t delivered_callbacks() const noexcept;

 private:
  mutable std::mutex mutex_;
  Callback callback_;
  std::atomic<bool> cancelled_{false};
  std::atomic<bool> completed_{false};
  std::atomic<bool> disposed_{false};
  std::atomic<std::uint64_t> delivered_callbacks_{0};
};

}  // namespace digitor

#endif
