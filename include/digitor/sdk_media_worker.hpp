#ifndef DIGITOR_SDK_MEDIA_WORKER_HPP
#define DIGITOR_SDK_MEDIA_WORKER_HPP

#include "digitor/async_operation.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace digitor {

enum class SdkMediaJobKind { preview, export_job };

struct SdkMediaProgress {
  SdkMediaJobKind kind{SdkMediaJobKind::preview};
  std::uint64_t completed_units{};
  std::uint64_t total_units{};
};

class SdkMediaWorker final {
 public:
  using ProgressCallback = std::function<void(const SdkMediaProgress&)>;
  using CompletionCallback = AsyncOperation::Callback;

  SdkMediaWorker(SdkMediaJobKind kind, std::uint64_t total_units,
                 ProgressCallback progress, CompletionCallback completion);
  ~SdkMediaWorker();

  SdkMediaWorker(const SdkMediaWorker&) = delete;
  SdkMediaWorker& operator=(const SdkMediaWorker&) = delete;

  bool start();
  bool cancel() noexcept;
  void dispose() noexcept;
  void join() noexcept;

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] std::uint64_t delivered_callbacks() const noexcept;

 private:
  void run() noexcept;

  SdkMediaJobKind kind_;
  std::uint64_t total_units_{};
  ProgressCallback progress_;
  std::shared_ptr<AsyncOperation> operation_;
  std::thread worker_;
  mutable std::mutex mutex_;
  std::atomic<bool> started_{false};
  std::atomic<bool> disposed_{false};
};

}  // namespace digitor

#endif
