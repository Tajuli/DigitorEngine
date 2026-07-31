#pragma once
#include "digitor/windows_zero_copy_runtime.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace digitor {
struct WindowsZeroCopyBrokerStats {
  std::uint64_t decode_requests{};
  std::uint64_t cache_hits{};
  std::uint64_t cache_misses{};
  std::uint64_t preview_deliveries{};
  std::uint64_t export_deliveries{};
  std::uint64_t identity_violations{};
  std::size_t live_frames{};
};

class WindowsZeroCopyFrameBroker final {
public:
  explicit WindowsZeroCopyFrameBroker(WindowsZeroCopyDecodeCallback,
                                      std::size_t capacity = 8);
  ~WindowsZeroCopyFrameBroker();
  [[nodiscard]] DigitorResult acquire(std::int64_t timestamp_us,
                                      ProcessedGpuFramePtr&) noexcept;
  [[nodiscard]] DigitorResult deliver_preview(std::int64_t timestamp_us,
                                              const WindowsZeroCopyFrameConsumer&) noexcept;
  [[nodiscard]] DigitorResult deliver_export(std::int64_t timestamp_us,
                                             const WindowsZeroCopyFrameConsumer&) noexcept;
  void clear() noexcept;
  [[nodiscard]] WindowsZeroCopyBrokerStats stats() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace digitor
