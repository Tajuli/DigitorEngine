#pragma once

#include <cstdint>
#include <string>

namespace digitor::internal {

class DeviceRemovalCheckpointTracker {
public:
  bool observe(const char* checkpoint, std::int32_t result) {
    if (first_failure_.empty() && result < 0)
      first_failure_ = checkpoint;
    if (result >= 0)
      last_healthy_ = checkpoint;
    return result < 0;
  }
  [[nodiscard]] const std::string& first_failure() const noexcept {
    return first_failure_;
  }
  [[nodiscard]] const std::string& last_healthy() const noexcept {
    return last_healthy_;
  }
private:
  std::string first_failure_;
  std::string last_healthy_;
};

} // namespace digitor::internal
